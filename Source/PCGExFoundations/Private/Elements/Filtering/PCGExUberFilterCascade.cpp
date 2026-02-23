// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Filtering/PCGExUberFilterCascade.h"

#include "Data/PCGExData.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExPointIO.h"
#include "Containers/PCGExScopedContainers.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExUberFilterCascade"
#define PCGEX_NAMESPACE UberFilterCascade

#pragma region UPCGExUberFilterCascadeSettings

#if WITH_EDITOR
void UPCGExUberFilterCascadeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	InputLabels.Reset(NumBranches);
	OutputLabels.Reset(NumBranches);

	for (int i = 0; i < NumBranches; i++)
	{
		FString SI = FString::Printf(TEXT("%d"), i);
		InputLabels.Emplace(FName(TEXT("→ ") + SI));
		OutputLabels.Emplace(FName(SI + TEXT(" →")));
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool UPCGExUberFilterCascadeSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	for (const FName& Label : InputLabels)
	{
		if (InPin->Properties.Label == Label) { return InPin->EdgeCount() > 0; }
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

bool UPCGExUberFilterCascadeSettings::HasDynamicPins() const { return true; }

TArray<FPCGPinProperties> UPCGExUberFilterCascadeSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	for (int i = 0; i < NumBranches; i++)
	{
		PCGEX_PIN_FILTERS(InputLabels[i], "Filters for this branch. Points matching these filters (and not claimed by a previous branch) are routed here.", Normal)
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExUberFilterCascadeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	PCGEX_PIN_POINTS(PCGExFilters::Labels::OutputOutsideFiltersLabel, "Points that didn't pass any branch's filters.", Normal)

	for (int i = 0; i < NumBranches; i++)
	{
		PCGEX_PIN_POINTS(OutputLabels[i], "Points that matched this branch's filters.", Normal)
	}

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(UberFilterCascade)
PCGEX_ELEMENT_BATCH_POINT_IMPL(UberFilterCascade)

FName UPCGExUberFilterCascadeSettings::GetMainOutputPin() const
{
	return PCGExFilters::Labels::OutputOutsideFiltersLabel;
}

#pragma endregion

#pragma region FPCGExUberFilterCascadeElement

bool FPCGExUberFilterCascadeElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCascade)

	Context->BranchFilterFactories.SetNum(Settings->NumBranches);

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		GetInputFactories(Context, Settings->InputLabels[i], Context->BranchFilterFactories[i], PCGExFactories::PointFilters, false);
	}

	Context->BranchOutputs.SetNum(Settings->NumBranches);
	for (int i = 0; i < Settings->NumBranches; i++)
	{
		Context->BranchOutputs[i] = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->BranchOutputs[i]->OutputPin = Settings->OutputLabels[i];
	}

	Context->DefaultOutput = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->DefaultOutput->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;

	return true;
}

bool FPCGExUberFilterCascadeElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterCascadeElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCascade)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->NumPairs = Context->MainPoints->Pairs.Num();

		for (int i = 0; i < Settings->NumBranches; i++)
		{
			Context->BranchOutputs[i]->Pairs.Init(nullptr, Context->NumPairs);
		}
		Context->DefaultOutput->Pairs.Init(nullptr, Context->NumPairs);

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to filter."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		Context->BranchOutputs[i]->PruneNullEntries(true);
	}
	Context->DefaultOutput->PruneNullEntries(true);

	// Pin layout: Outside (0), branches (1..N)
	uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;

	if (Settings->bOutputDiscardedElements)
	{
		if (!Context->DefaultOutput->StageOutputs()) { Mask |= 1ULL << 0; }
	}
	else
	{
		Mask |= 1ULL << 0;
	}

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		if (!Context->BranchOutputs[i]->StageOutputs()) { Mask |= 1ULL << (i + 1); }
	}

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExUberFilterCascade::FProcessor

namespace PCGExUberFilterCascade
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExUberFilterCascade::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::NoInit)

		const int32 NumBranches = Settings->NumBranches;
		BranchManagers.SetNum(NumBranches);

		for (int i = 0; i < NumBranches; i++)
		{
			if (!Context->BranchFilterFactories[i].IsEmpty())
			{
				PCGEX_MAKE_SHARED(Manager, PCGExPointFilter::FManager, PointDataFacade)
				if (Manager->Init(Context, Context->BranchFilterFactories[i]))
				{
					BranchManagers[i] = Manager;
				}
			}
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		const int32 NumBranches = Settings->NumBranches;
		const int32 MaxRange = PCGExMT::FScope::GetMaxRange(Loops);
		const int32 TotalBuckets = NumBranches + 1; // N branches + default

		BranchIndices.SetNum(TotalBuckets);
		BranchCounts.Init(0, TotalBuckets);

		for (int i = 0; i < TotalBuckets; i++)
		{
			BranchIndices[i] = MakeShared<PCGExMT::TScopedArray<int32>>(Loops);
			BranchIndices[i]->Reserve(MaxRange);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::UberFilterCascade::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		const int32 NumBranches = BranchManagers.Num();
		const int32 DefaultIdx = NumBranches; // Last bucket

		PCGEX_SCOPE_LOOP(Index)
		{
			int32 MatchedBranch = -1;
			for (int32 i = 0; i < NumBranches; i++)
			{
				if (BranchManagers[i] && BranchManagers[i]->Test(Index))
				{
					MatchedBranch = i;
					break;
				}
			}

			if (MatchedBranch >= 0)
			{
				BranchIndices[MatchedBranch]->Get_Ref(Scope).Add(Index);
				FPlatformAtomics::InterlockedAdd(&BranchCounts[MatchedBranch], 1);
			}
			else
			{
				BranchIndices[DefaultIdx]->Get_Ref(Scope).Add(Index);
				FPlatformAtomics::InterlockedAdd(&BranchCounts[DefaultIdx], 1);
			}
		}
	}

	TSharedPtr<PCGExData::FPointIO> FProcessor::CreateIO(const TSharedRef<PCGExData::FPointIOCollection>& InCollection, const PCGExData::EIOInit InitMode) const
	{
		TSharedPtr<PCGExData::FPointIO> NewPointIO = PCGExData::NewPointIO(PointDataFacade->Source, InCollection->OutputPin);

		if (!NewPointIO->InitializeOutput(InitMode)) { return nullptr; }

		InCollection->Pairs[BatchIndex] = NewPointIO;
		return NewPointIO;
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterCascadeProcessor::CompleteWork);

		const int32 NumBranches = Settings->NumBranches;
		const int32 DefaultIdx = NumBranches;
		const int32 NumPoints = PointDataFacade->GetNum();

		// Check if all points went to a single bucket (can use Forward for zero-copy)
		int32 SingleBucket = -1;
		for (int32 i = 0; i <= DefaultIdx; i++)
		{
			if (BranchCounts[i] == NumPoints)
			{
				SingleBucket = i;
				break;
			}
		}

		if (SingleBucket >= 0)
		{
			// All points in one bucket -- Forward (zero-copy)
			if (SingleBucket == DefaultIdx)
			{
				if (!Settings->bOutputDiscardedElements) { return; }
				(void)CreateIO(Context->DefaultOutput.ToSharedRef(), PCGExData::EIOInit::Forward);
			}
			else
			{
				(void)CreateIO(Context->BranchOutputs[SingleBucket].ToSharedRef(), PCGExData::EIOInit::Forward);
			}
			return;
		}

		// Mixed distribution -- create new outputs per bucket
		for (int32 i = 0; i < NumBranches; i++)
		{
			if (BranchCounts[i] <= 0) { continue; }

			TArray<int32> ReadIndices;
			BranchIndices[i]->Collapse(ReadIndices);

			TSharedPtr<PCGExData::FPointIO> BranchIO = CreateIO(Context->BranchOutputs[i].ToSharedRef(), PCGExData::EIOInit::New);
			if (!BranchIO) { continue; }

			PCGExPointArrayDataHelpers::SetNumPointsAllocated(BranchIO->GetOut(), ReadIndices.Num(), BranchIO->GetAllocations());
			BranchIO->InheritProperties(ReadIndices, BranchIO->GetAllocations());
		}

		// Default bucket
		if (BranchCounts[DefaultIdx] > 0 && Settings->bOutputDiscardedElements)
		{
			TArray<int32> ReadIndices;
			BranchIndices[DefaultIdx]->Collapse(ReadIndices);

			TSharedPtr<PCGExData::FPointIO> DefaultIO = CreateIO(Context->DefaultOutput.ToSharedRef(), PCGExData::EIOInit::New);
			if (!DefaultIO) { return; }

			PCGExPointArrayDataHelpers::SetNumPointsAllocated(DefaultIO->GetOut(), ReadIndices.Num(), DefaultIO->GetAllocations());
			DefaultIO->InheritProperties(ReadIndices, DefaultIO->GetAllocations());
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
