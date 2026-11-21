// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/PCGExUberNoise.h"

#include "PCGExBlendingCommon.h"
#include "PCGExVersion.h"
#include "Async/ParallelFor.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExProxyDataBlending.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"
#include "Data/PCGExProxyDataImpl.h"
#include "Data/PCGExSubAccessor.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExNoiseGenerator.h"


#define LOCTEXT_NAMESPACE "PCGExUberNoise"
#define PCGEX_NAMESPACE UberNoise

#if WITH_EDITOR
FString UPCGExUberNoiseSettings::GetDisplayName() const
{
	if (Attributes.WantsRemappedOutput())
	{
		return TEXT("UN : ") + Attributes.Source.ToString() + TEXT(" → ") + Attributes.Target.ToString();
	}

	return TEXT("Uber Noise : ") + Attributes.Source.ToString();
}
#endif

PCGEX_INITIALIZE_ELEMENT(UberNoise)

TArray<FPCGPinProperties> UPCGExUberNoiseSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExNoise3D::Labels::SourceNoise3DLabel, "Noises", Required, FPCGExDataTypeInfoNoise3D::AsId())
	return PinProperties;
}

PCGExData::EIOInit UPCGExUberNoiseSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(UberNoise)

bool FPCGExUberNoiseElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(UberNoise)

	if (!Settings->Attributes.ValidateNamesOrProperties(Context))
	{
		return false;
	}

	Context->NoiseGenerator = MakeShared<PCGExNoise3D::FNoiseGenerator>();
	if (!Context->NoiseGenerator->Init(Context))
	{
		return false;
	}

	return true;
}

bool FPCGExUberNoiseElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberNoiseElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(UberNoise)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any data to add noise to."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExUberNoise
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		EPCGExABBlendingType BlendMode = Settings->BlendMode;

		PCGExData::FProxyDescriptor A;
		PCGExData::FProxyDescriptor C;
		C.DataFacade = PointDataFacade;
		C.Side = PCGExData::EIOSide::Out;
		C.Role = PCGExData::EProxyRole::Write;

		EPCGMetadataTypes NoiseType = EPCGMetadataTypes::Unknown;

		const bool bIsNewOutput = Settings->Mode == EPCGExUberNoiseMode::New;

		if (bIsNewOutput)
		{
			// We don't care about InDescriptor

			C.RealType = Settings->OutputType;
			BlendMode = EPCGExABBlendingType::CopySource;

			if (!C.Capture(Context, Settings->Attributes.GetTargetSelector(), PCGExData::EIOSide::Out, false))
			{
				C.WorkingType = C.RealType = Settings->OutputType;
			}
		}
		else
		{
			WeightBuffer = Settings->SourceValueWeight.GetValueSetting();
			if (!WeightBuffer->Init(PointDataFacade))
			{
				return false;
			}

			A.DataFacade = PointDataFacade;
			A.Role = PCGExData::EProxyRole::Read;

			if (!A.CaptureStrict(Context, Settings->Attributes.GetSourceSelector(), PCGExData::EIOSide::In, true))
			{
				return false;
			}

			if (Settings->Attributes.bOutputToDifferentName)
			{
				if (!C.Capture(Context, Settings->Attributes.GetTargetSelector(), PCGExData::EIOSide::Out, false))
				{
					if (C.RealType == EPCGMetadataTypes::Unknown)
					{
						C.RealType = A.WorkingType;
					}
					if (C.WorkingType == EPCGMetadataTypes::Unknown)
					{
						C.WorkingType = A.WorkingType;
					}
				}
			}
			else
			{
				C = A;
			}
		}

		C.Side = PCGExData::EIOSide::Out;
		C.Role = PCGExData::EProxyRole::Write;

		NoiseType = PCGExNoise3D::GetNoise3DType(PCGExData::GetNumFieldsForType(C.WorkingType));
		if (NoiseType == EPCGMetadataTypes::Unknown)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Could not infer noise type."));
			return false;
		}

		PCGExData::FProxyDescriptor N = C;
		N.Role = PCGExData::EProxyRole::Read;
		N.RealType = NoiseType;
		N.WorkingType = NoiseType;
		N.AddFlags(PCGExData::EProxyFlags::Raw);

		A.WorkingType = NoiseType;
		N.WorkingType = NoiseType;
		C.WorkingType = NoiseType;

		if (bIsNewOutput)
		{
			Blender = PCGExBlending::CreateProxyBlender(Context, BlendMode, N, C);
		}
		else
		{
			Blender = PCGExBlending::CreateProxyBlender(Context, BlendMode, A, N, C);
		}

		if (!Blender)
		{
			return false;
		}

		NoiseBuffer = bIsNewOutput ? Blender->A : Blender->B;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberNoise::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		TArray<FVector> Positions;
		PCGExArrayHelpers::InitArray(Positions, Scope.Count);
		for (int i = 0; i < Scope.Count; i++)
		{
			Positions[i] = InTransforms[Scope.Start + i].GetLocation();
		}

		TArray<double> Weights;
		if (WeightBuffer)
		{
			Weights.SetNumUninitialized(Scope.Count);
			WeightBuffer->ReadScope(Scope.Start, Weights);
		}
		else
		{
			Weights.Init(1, Scope.Count);
		}

#define PCGEX_LOOP_NOISE(_TYPE) \
	{\
		TArray<_TYPE>& TypedNoiseBuffer = *StaticCastSharedPtr<PCGExData::TRawBufferProxy<_TYPE>>(NoiseBuffer)->Buffer.Get(); \
		Context->NoiseGenerator->Generate(Positions, TArrayView<_TYPE>(TypedNoiseBuffer.GetData() + Scope.Start, Scope.Count)); \
		Blender->BlendScope(Scope, Scope.GetConstView<int8>(PointFilterCache), Weights); \
	}

		switch (NoiseBuffer->RealType)
		{
		default: /* no-op */ break;
		case EPCGMetadataTypes::Double: PCGEX_LOOP_NOISE(double)
		break;
		case EPCGMetadataTypes::Vector2: PCGEX_LOOP_NOISE(FVector2D)
		break;
		case EPCGMetadataTypes::Vector: PCGEX_LOOP_NOISE(FVector)
		break;
		case EPCGMetadataTypes::Vector4: PCGEX_LOOP_NOISE(FVector4)
		break;
		}

#undef PCGEX_LOOP_NOISE
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
