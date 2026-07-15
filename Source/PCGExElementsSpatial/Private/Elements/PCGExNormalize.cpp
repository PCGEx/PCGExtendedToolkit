// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExNormalize.h"


#include "Async/ParallelFor.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGExProxyData.h"
#include "Data/PCGExProxyDataHelpers.h"
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExMathBounds.h"
#include "Sampling/PCGExSamplingCommon.h"


#define LOCTEXT_NAMESPACE "PCGExNormalizeElement"
#define PCGEX_NAMESPACE Normalize

#if WITH_EDITOR
void UPCGExNormalizeSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		// Rewire Transform
		PCGEX_SHORTHAND_RENAME_PIN(TransformAttribute, TransformConstant, Transform)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExNormalizeSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		Transform.Update(TransformInput_DEPRECATED, TransformAttribute_DEPRECATED, TransformConstant_DEPRECATED);
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

UPCGExNormalizeSettings::UPCGExNormalizeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (Output.GetName() == TEXT("@Last"))
	{
		Output.Update("$Position");
	}
	// Deprecated selector must default to the old ctor-set value so unserialized (default)
	// legacy assets migrate to the same selection.
	if (TransformAttribute_DEPRECATED.GetName() == TEXT("@Last"))
	{
		TransformAttribute_DEPRECATED.Update("@Data.Transform");
	}
}

PCGEX_INITIALIZE_ELEMENT(Normalize)
PCGEX_ELEMENT_BATCH_POINT_IMPL(Normalize)

TArray<FPCGPinProperties> UPCGExNormalizeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceBoundsLabel, "Point data that will be used as unified bounds for all inputs", Normal)
	return PinProperties;
}

bool UPCGExNormalizeSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExCommon::Labels::SourceBoundsLabel)
	{
		return InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

PCGExData::EIOInit UPCGExNormalizeSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

bool FPCGExNormalizeElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(Normalize)

	TArray<FPCGTaggedData> UnifiedBounds = Context->InputData.GetSpatialInputsByPin(PCGExCommon::Labels::SourceBoundsLabel);
	for (int i = 0; i < UnifiedBounds.Num(); ++i)
	{
		if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(UnifiedBounds[i].Data))
		{
			Context->bUseUnifiedBounds = true;
			Context->UnifiedBounds += PCGExMath::GetBounds(PointData, Settings->BoundsSource);
		}
	}

	return true;
}

bool FPCGExNormalizeElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExNormalizeElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(Normalize)
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
			return Context->CancelExecution(TEXT("No data."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExNormalize
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExNormalize::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		TransformBuffer = Settings->Transform.GetValueSetting();
		if (!TransformBuffer->Init(PointDataFacade, true))
		{
			return false;
		}

		Box = Context->bUseUnifiedBounds ? Context->UnifiedBounds : PCGExMath::GetBounds(PointDataFacade->GetIn(), Settings->BoundsSource);
		Size = Box.GetSize();

		OneMinus[0] = EnumHasAnyFlags(static_cast<EPCGExApplySampledComponentFlags>(Settings->OneMinus), EPCGExApplySampledComponentFlags::X);
		OneMinus[1] = EnumHasAnyFlags(static_cast<EPCGExApplySampledComponentFlags>(Settings->OneMinus), EPCGExApplySampledComponentFlags::Y);
		OneMinus[2] = EnumHasAnyFlags(static_cast<EPCGExApplySampledComponentFlags>(Settings->OneMinus), EPCGExApplySampledComponentFlags::Z);

		PCGExData::FProxyDescriptor Descriptor = PCGExData::FProxyDescriptor();
		Descriptor.DataFacade = PointDataFacade;
		Descriptor.Role = PCGExData::EProxyRole::Write;

		Descriptor.Capture(Context, Settings->Output, PCGExData::EIOSide::Out, false);

		Descriptor.WorkingType = EPCGMetadataTypes::Vector;
		if (Descriptor.RealType == EPCGMetadataTypes::Unknown)
		{
			Descriptor.RealType = EPCGMetadataTypes::Vector;
		}

		OutputBuffer = PCGExData::GetProxyBuffer(Context, Descriptor);

		if (!OutputBuffer.IsValid())
		{
			return false;
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::Normalize::ProcessPoints);

			const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

			using FWrapFn = std::function<double(const double)>;

			FWrapFn Wrap = [](const double Value)-> double
			{
				return Value;
			};


			switch (Settings->Wrapping)
			{
			case EPCGExIndexSafety::Ignore:
				break;
			case EPCGExIndexSafety::Tile:
				Wrap = [](const double Value)-> double
				{
					constexpr double OnePlus = 1 + UE_SMALL_NUMBER;
					const double W = FMath::Fmod(Value, OnePlus);
					return W < 0 ? W + OnePlus : W;
				};
				break;
			case EPCGExIndexSafety::Clamp:
				Wrap = [](const double Value)-> double
				{
					return FMath::Clamp(Value, 0, 1);
				};
				break;
			case EPCGExIndexSafety::Yoyo:
				Wrap = [](const double Value)-> double
				{
					double C = FMath::Fmod(Value, 2);
					C = C < 0 ? C + 2 : C;
					return C <= 1 ? C : 2 - C;
				};
				break;
			}

			PCGExMT::ParallelOrSequential(
				InTransforms.Num(),
				[&](const int32 i)
				{
					FVector UVW = Settings->Offset + ((TransformBuffer->Read(i).TransformPosition(InTransforms[i].GetLocation()) - Box.Min) * Settings->Tile) / Size;
					for (int j = 0; j < 3; j++)
					{
						UVW[j] = Wrap(UVW[j]);
						if (OneMinus[j])
						{
							UVW[j] = 1 - UVW[j];
						}
					}
					OutputBuffer->Set(i, UVW);
				});

			PointDataFacade->WriteFastest(TaskManager);

			return true;
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
