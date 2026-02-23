// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExSubdivide.h"


#include "Helpers/PCGExRandomHelpers.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"


#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"

#define LOCTEXT_NAMESPACE "PCGExSubdivideElement"
#define PCGEX_NAMESPACE Subdivide

PCGEX_SETTING_VALUE_IMPL(UPCGExSubdivideSettings, SubdivisionAmount, double, AmountInput, SubdivisionAmount, SubdivideMethod == EPCGExSubdivideMode::Count ? Count : Distance)

#if WITH_EDITORONLY_DATA
void UPCGExSubdivideSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Blending) { Blending = NewObject<UPCGExSubPointsBlendInterpolate>(this, TEXT("Blending")); }
	}
	Super::PostInitProperties();
}
#endif

TArray<FPCGPinProperties> UPCGExSubdivideSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExBlending::Labels::SourceOverridesBlendingOps)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(Subdivide)
PCGEX_ELEMENT_BATCH_POINT_IMPL(Subdivide)

bool FPCGExSubdivideElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(Subdivide)

	if (Settings->bFlagSubPoints) { PCGEX_VALIDATE_NAME(Settings->SubPointFlagName) }
	if (Settings->bWriteAlpha) { PCGEX_VALIDATE_NAME(Settings->AlphaAttributeName) }

	PCGEX_BIND_INSTANCED_FACTORY(Blending, UPCGExSubPointsBlendInstancedFactory, PCGExBlending::Labels::SourceOverridesBlendingOps)

	return true;
}


bool FPCGExSubdivideElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSubdivideElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(Subdivide)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 2 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					Entry->InitializeOutput(PCGExData::EIOInit::Forward);
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any paths to subdivide."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

namespace PCGExSubdivide
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSubdivide::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::New)

		bClosedLoop = PCGExPaths::Helpers::GetClosedLoop(PointDataFacade->GetOut());

		if (Settings->SubdivideMethod == EPCGExSubdivideMode::Manhattan)
		{
			ManhattanDetails = Settings->ManhattanDetails;
			if (!ManhattanDetails.Init(Context, PointDataFacade)) { return false; }

			ManhattanPoints.Init(nullptr, PointDataFacade->GetNum());
			bIsManhattan = true;
		}
		else
		{
			AmountGetter = Settings->GetValueSettingSubdivisionAmount();
			if (!AmountGetter->Init(PointDataFacade)) { return false; }
		}

		bUseCount = Settings->SubdivideMethod == EPCGExSubdivideMode::Count;

		SubBlending = Context->Blending->CreateOperation();
		SubBlending->bClosedLoop = bClosedLoop;

		PCGExArrayHelpers::InitArray(Subdivisions, PointDataFacade->GetNum());

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::Subdivide::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		PCGEX_SCOPE_LOOP(Index)
		{
			const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

			FSubdivision& Sub = Subdivisions[Index];

			Sub.NumSubdivisions = 0;
			Sub.InStart = Index;
			Sub.InEnd = Index + 1 == PointIO->GetNum() ? 0 : Index + 1;
			Sub.Dist = FVector::Distance(InTransforms[Sub.InEnd].GetLocation(), InTransforms[Sub.InStart].GetLocation());

			if (!PointFilterCache[Index]) { continue; }

			if (bIsManhattan)
			{
				TSharedPtr<TArray<FVector>> Subs = MakeShared<TArray<FVector>>();
				TArray<FVector>& SubPoints = *Subs.Get();
				Sub.NumSubdivisions = ManhattanDetails.ComputeSubdivisions(InTransforms[Sub.InStart].GetLocation(), InTransforms[Sub.InEnd].GetLocation(), Index, SubPoints, Sub.Dist);

				if (Sub.NumSubdivisions > 0) { ManhattanPoints[Index] = Subs; }

				continue;
			}

			double Amount = AmountGetter->Read(Index);
			bool bRedistribute = bUseCount;

			if (!bRedistribute)
			{
				Sub.NumSubdivisions = FMath::Floor(Sub.Dist / Amount);
				Sub.StepSize = Amount;

				if (Settings->bRedistributeEvenly)
				{
					Sub.StartOffset = (Sub.Dist - (Sub.StepSize * (Sub.NumSubdivisions - 1))) * 0.5;
					bRedistribute = true;
					Amount = Sub.NumSubdivisions;
				}
				else
				{
					Sub.StartOffset = Sub.StepSize;
				}
			}

			if (bRedistribute)
			{
				Sub.NumSubdivisions = FMath::Floor(Amount);
				Sub.StepSize = Sub.Dist / static_cast<double>(Sub.NumSubdivisions + 1);
				Sub.StartOffset = Sub.StepSize;
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		const TSharedRef<PCGExData::FPointIO>& PointIO = PointDataFacade->Source;

		int32 NumPoints = 0;

		if (!bClosedLoop) { Subdivisions[Subdivisions.Num() - 1].NumSubdivisions = 0; }

		for (FSubdivision& Sub : Subdivisions)
		{
			Sub.OutStart = NumPoints++;
			NumPoints += Sub.NumSubdivisions;
			Sub.OutEnd = NumPoints;
		}

		if (bClosedLoop) { Subdivisions[Subdivisions.Num() - 1].OutEnd = 0; }
		else { Subdivisions[Subdivisions.Num() - 1].NumSubdivisions = 0; }

		if (NumPoints == PointIO->GetNum())
		{
			PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::Duplicate)

			if (Settings->bFlagSubPoints) { WriteMark(PointIO, Settings->SubPointFlagName, false); }
			if (Settings->bWriteAlpha) { WriteMark(PointIO, Settings->AlphaAttributeName, Settings->DefaultAlpha); }
			return;
		}

		PCGEX_INIT_IO_VOID(PointIO, PCGExData::EIOInit::New)

		const UPCGBasePointData* InPoints = PointIO->GetIn();
		UPCGBasePointData* MutablePoints = PointIO->GetOut();

		UPCGMetadata* Metadata = PointIO->GetOut()->Metadata;

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumPoints, InPoints->GetAllocatedProperties());

		TConstPCGValueRange<int64> InMetadataEntries = InPoints->GetConstMetadataEntryValueRange();
		TPCGValueRange<int64> OutMetadataEntries = MutablePoints->GetMetadataEntryValueRange();

		TArray<int32> WriteIndices;
		WriteIndices.SetNum(InMetadataEntries.Num());

		for (int i = 0; i < Subdivisions.Num(); i++)
		{
			const FSubdivision& Sub = Subdivisions[i];
			WriteIndices[i] = Sub.OutStart;

			OutMetadataEntries[Sub.OutStart] = InMetadataEntries[i];
			Metadata->InitializeOnSet(OutMetadataEntries[Sub.OutStart]);

			if (Sub.NumSubdivisions == 0) { continue; }

			const int32 SubStart = Sub.OutStart + 1;

			for (int s = 0; s < Sub.NumSubdivisions; s++)
			{
				OutMetadataEntries[SubStart + s] = PCGInvalidEntryKey;
				Metadata->InitializeOnSet(OutMetadataEntries[SubStart + s]);
			}
		}

		PointDataFacade->Source->InheritPoints(WriteIndices);

		if (Settings->bFlagSubPoints)
		{
			FlagWriter = PointDataFacade->GetWritable<bool>(Settings->SubPointFlagName, false, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->SubPointFlagName);
		}

		if (Settings->bWriteAlpha)
		{
			AlphaWriter = PointDataFacade->GetWritable<double>(Settings->AlphaAttributeName, Settings->DefaultAlpha, true, PCGExData::EBufferInit::New);
			ProtectedAttributes.Add(Settings->AlphaAttributeName);
		}

		if (!SubBlending->PrepareForData(Context, PointDataFacade, &ProtectedAttributes))
		{
			//
			bIsProcessorValid = false;
			return;
		}

		StartParallelLoopForRange(Subdivisions.Num());
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();
		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = PointDataFacade->GetOut()->GetSeedValueRange(false);

		PCGEX_SCOPE_LOOP(Index)
		{
			const FSubdivision& Sub = Subdivisions[Index];
			const TSharedPtr<TArray<FVector>> ManhattanSubdivs = bIsManhattan ? ManhattanPoints[Index] : nullptr;

			if (FlagWriter) { FlagWriter->SetValue(Sub.OutStart, false); }
			if (AlphaWriter) { AlphaWriter->SetValue(Sub.OutStart, Settings->DefaultAlpha); }

			if (Sub.NumSubdivisions == 0) { continue; }

			const FVector Start = InTransforms[Sub.InStart].GetLocation();
			const FVector End = InTransforms[Sub.InEnd].GetLocation();
			const FVector Dir = (End - Start).GetSafeNormal();

			PCGExPaths::FPathMetrics Metrics = PCGExPaths::FPathMetrics(Start);

			const int32 SubStart = Sub.OutStart + 1;
			for (int s = 0; s < Sub.NumSubdivisions; s++)
			{
				const int32 SubIndex = SubStart + s;

				if (FlagWriter) { FlagWriter->SetValue(SubIndex, true); }

				const FVector Position = bIsManhattan ? *(ManhattanSubdivs->GetData() + s) : Start + Dir * (Sub.StartOffset + s * Sub.StepSize);
				OutTransforms[SubIndex].SetLocation(Position);
				const double Alpha = Metrics.Add(Position) / Sub.Dist;
				if (AlphaWriter) { AlphaWriter->SetValue(SubStart + s, Alpha); }
			}

			Metrics.Add(End);

			PCGExData::FScope SubScope = PointDataFacade->GetOutScope(SubStart, Sub.NumSubdivisions);
			SubBlending->ProcessSubPoints(PointDataFacade->GetOutPoint(Sub.OutStart), PointDataFacade->GetOutPoint(Sub.OutEnd), SubScope, Metrics);

			for (int i = Sub.OutStart + 1; i < Sub.OutEnd; i++) { OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[i].GetLocation()); }
		}
	}

	void FProcessor::Write()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
