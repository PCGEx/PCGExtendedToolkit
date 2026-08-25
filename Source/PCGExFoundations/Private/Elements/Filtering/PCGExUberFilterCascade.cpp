// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Filtering/PCGExUberFilterCascade.h"

#include "PCGParamData.h"
#include "Containers/PCGExScopedContainers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExBucketDispatchHelpers.h"


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
		if (InPin->Properties.Label == Label)
		{
			return InPin->EdgeCount() > 0;
		}
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

bool UPCGExUberFilterCascadeSettings::HasDynamicPins() const
{
	return true;
}

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
	if (Mode == EPCGExUberFilterMode::Write)
	{
		// Single "Out" pin: all points forwarded with the partition index written to an attribute.
		return Super::OutputPinProperties();
	}

	TArray<FPCGPinProperties> PinProperties;

	PCGEX_PIN_ANY(PCGExFilters::Labels::OutputOutsideFiltersLabel, "Elements that didn't pass any branch's filters.", Normal)

	for (int i = 0; i < NumBranches; i++)
	{
		PCGEX_PIN_ANY(OutputLabels[i], "Elements that matched this branch's filters.", Normal)
	}

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(UberFilterCascade)
PCGEX_ELEMENT_BATCH_POINT_IMPL(UberFilterCascade)

PCGExData::EIOInit UPCGExUberFilterCascadeSettings::GetMainDataInitializationPolicy() const
{
	if (Mode == EPCGExUberFilterMode::Write)
	{
		return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
	}
	return PCGExData::EIOInit::NoInit;
}

FName UPCGExUberFilterCascadeSettings::GetMainOutputPin() const
{
	// Partition mode: the first output pin (Outside) is the main output, ensuring a proper forward when the node is disabled.
	// Write mode: use the default single "Out" pin.
	return Mode == EPCGExUberFilterMode::Partition ? PCGExFilters::Labels::OutputOutsideFiltersLabel : Super::GetMainOutputPin();
}

PCGExData::EIOHandling UPCGExUberFilterCascadeSettings::GetMainDataHandling() const
{
	return PCGExData::EIOHandling::Dynamic;
}

#pragma endregion

#pragma region FPCGExUberFilterCascadeElement

bool FPCGExUberFilterCascadeElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCascade)

	Context->BranchFilterFactories.SetNum(Settings->NumBranches);

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		PCGExFactories::GetInputFactories(Context, Settings->InputLabels[i], Context->BranchFilterFactories[i], PCGExFactories::PointFilters(), false);
	}

	if (Settings->Mode == EPCGExUberFilterMode::Write)
	{
		PCGEX_VALIDATE_NAME(Settings->PartitionAttributeName)
		return true;
	}

	Context->BranchOutputs.SetNum(Settings->NumBranches);
	for (int i = 0; i < Settings->NumBranches; i++)
	{
		Context->BranchOutputs[i] = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->BranchOutputs[i]->OutputPin = Settings->OutputLabels[i];
	}

	if (Settings->bOutputDiscardedElements)
	{
		Context->DefaultOutput = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->DefaultOutput->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;
	}

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

		if (Settings->Mode == EPCGExUberFilterMode::Partition)
		{
			for (int i = 0; i < Settings->NumBranches; i++)
			{
				Context->BranchOutputs[i]->Pairs.Init(nullptr, Context->NumPairs);
			}
			if (Context->DefaultOutput)
			{
				Context->DefaultOutput->Pairs.Init(nullptr, Context->NumPairs);
			}
		}

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
			return Context->CancelExecution(TEXT("Could not find any points to filter."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->Mode == EPCGExUberFilterMode::Write)
	{
		Context->MainPoints->StageOutputs();
		return Context->TryComplete();
	}

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		Context->BranchOutputs[i]->PruneNullEntries(true);
	}
	if (Context->DefaultOutput)
	{
		Context->DefaultOutput->PruneNullEntries(true);
	}

	// Pin layout: Outside (0), branches (1..N)
	uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;

	if (Context->DefaultOutput)
	{
		if (!Context->DefaultOutput->StageOutputs())
		{
			Mask |= 1ULL << 0;
		}
	}
	else
	{
		// Outside pin always exists in the layout (see OutputPinProperties); deactivate it when
		// bOutputDiscardedElements is off and DefaultOutput was never created.
		Mask |= 1ULL << 0;
	}

	for (int i = 0; i < Settings->NumBranches; i++)
	{
		if (!Context->BranchOutputs[i]->StageOutputs())
		{
			Mask |= 1ULL << (i + 1);
		}
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

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Attribute-set inputs are processed as their temp point conversion but output as param data.
		ParamSource = Cast<UPCGParamData>(PointDataFacade->Source->InitializationData);

		PCGEX_INIT_IO(PointDataFacade->Source, ParamSource ? PCGExData::EIOInit::NoInit : Settings->GetMainDataInitializationPolicy())

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

		if (Settings->Mode == EPCGExUberFilterMode::Write)
		{
			if (ParamSource)
			{
				ParamPartitionValues.SetNumUninitialized(PointDataFacade->GetNum());
			}
			else
			{
				PartitionBuffer = PointDataFacade->GetWritable<int32>(Settings->PartitionAttributeName, Settings->DefaultValue, false, PCGExData::EBufferInit::New);
			}
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		if (Settings->Mode != EPCGExUberFilterMode::Partition)
		{
			// Write mode writes directly to the partition buffer; no per-scope index buckets needed.
			return;
		}

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

		if (Settings->Mode == EPCGExUberFilterMode::Write)
		{
			const int32 Base = Settings->DefaultValue;

			if (ParamSource)
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					// Depth 0 = unmatched (writes Base); a point matching branch i writes Base + i + 1.
					int32 Depth = 0;
					for (int32 i = 0; i < NumBranches; i++)
					{
						if (BranchManagers[i] && BranchManagers[i]->Test(Index))
						{
							Depth = i + 1;
							break;
						}
					}

					ParamPartitionValues[Index] = Base + Depth;
				}
			}
			else
			{
				PCGEX_SCOPE_LOOP(Index)
				{
					// Depth 0 = unmatched (writes Base); a point matching branch i writes Base + i + 1.
					int32 Depth = 0;
					for (int32 i = 0; i < NumBranches; i++)
					{
						if (BranchManagers[i] && BranchManagers[i]->Test(Index))
						{
							Depth = i + 1;
							break;
						}
					}

					PartitionBuffer->SetValue(Index, Base + Depth);
				}
			}

			return;
		}

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

		if (!NewPointIO->InitializeOutput(InitMode))
		{
			return nullptr;
		}

		InCollection->Pairs[BatchIndex] = NewPointIO;
		return NewPointIO;
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterCascadeProcessor::CompleteWork);

		if (Settings->Mode == EPCGExUberFilterMode::Write)
		{
			if (ParamSource)
			{
				// Write onto the original under the StealData contract, otherwise onto a managed duplicate.
				const bool bSteal = Context->bWantsDataStealing;
				UPCGData* Target = bSteal
					                   ? const_cast<UPCGParamData*>(ParamSource)
					                   : Context->ManagedObjects->DuplicateData<UPCGData>(ParamSource);
				if (!Target)
				{
					return;
				}

				const int32 NumRows = ParamPartitionValues.Num();
				if (FPCGMetadataAttribute<int32>* PartitionAttribute = Target->MutableMetadata()->FindOrCreateAttribute<int32>(Settings->PartitionAttributeName, Settings->DefaultValue))
				{
					TArray<PCGMetadataEntryKey> Keys;
					Keys.SetNumUninitialized(NumRows);
					for (int i = 0; i < NumRows; i++)
					{
						Keys[i] = i;
					}

					PartitionAttribute->SetValues(Keys, ParamPartitionValues);
				}

				PointDataFacade->Source->SetOutputOverride(Target, !bSteal);
				return;
			}

			PointDataFacade->WriteFastest(TaskManager);
			return;
		}

		if (ParamSource)
		{
			PCGExBucketDispatchHelpers::DispatchParamBuckets(
				Context,
				ParamSource,
				Context->BranchOutputs,
				Context->DefaultOutput,
				BranchCounts,
				BranchIndices,
				PointDataFacade->GetNum(),
				[this](const TSharedRef<PCGExData::FPointIOCollection>& InCollection)
				{
					return CreateIO(InCollection, PCGExData::EIOInit::NoInit);
				});
			return;
		}

		PCGExBucketDispatchHelpers::DispatchBuckets(
			Context->BranchOutputs,
			Context->DefaultOutput,
			BranchCounts,
			BranchIndices,
			PointDataFacade->GetNum(),
			[this](const TSharedRef<PCGExData::FPointIOCollection>& InCollection, PCGExData::EIOInit InitMode)
			{
				return CreateIO(InCollection, InitMode);
			});
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
