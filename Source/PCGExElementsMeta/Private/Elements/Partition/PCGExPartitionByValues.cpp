// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Partition/PCGExPartitionByValues.h"


#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExPartitionByValuesBase"
#define PCGEX_NAMESPACE PartitionByValues

namespace PCGExPartitionByValuesBase
{
	const FName SourceLabel = TEXT("Source");
}

#if WITH_EDITOR
void UPCGExPartitionByValuesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (FPCGExPartitonRuleConfig& Config : PartitionRules)
	{
		Config.UpdateUserFacingInfos();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

bool UPCGExPartitionByValuesBaseSettings::GetMainAcceptMultipleData() const
{
	return false;
}

bool UPCGExPartitionByValuesBaseSettings::GetPartitionRules(FPCGExContext* InContext, TArray<FPCGExPartitonRuleConfig>& OutRules) const
{
	return true;
}

bool UPCGExPartitionByValuesSettings::GetPartitionRules(FPCGExContext* InContext, TArray<FPCGExPartitonRuleConfig>& OutRules) const
{
	if (PartitionRules.IsEmpty())
	{
		return false;
	}
	for (const FPCGExPartitonRuleConfig& Config : PartitionRules)
	{
		OutRules.Add(Config);
	}
	return true;
}

PCGEX_INITIALIZE_ELEMENT(PartitionByValuesBase)
PCGEX_ELEMENT_BATCH_POINT_IMPL(PartitionByValuesBase)

bool FPCGExPartitionByValuesBaseElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PartitionByValuesBase)

	TArray<FPCGExPartitonRuleConfig> Configs;
	if (!Settings->GetPartitionRules(InContext, Configs))
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No partitioning rules."));
		return false;
	}

	if (Settings->bWriteKeySum)
	{
		PCGEX_VALIDATE_NAME(Settings->KeySumAttributeName)
	}

	Context->RulesConfigs.Reserve(Configs.Num());

	for (const FPCGExPartitonRuleConfig& Config : Configs)
	{
		if (!Config.bEnabled)
		{
			continue;
		}

		PCGEX_VALIDATE_NAME_CONDITIONAL(Config.bWriteKey, Config.KeyAttributeName)
		PCGEX_VALIDATE_NAME_CONDITIONAL(Config.bWriteTag, Config.TagPrefixName)
		Context->RulesConfigs.Add(Config);
	}

	if (Context->RulesConfigs.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No partitioning rules."));
		return false;
	}

	return true;
}

bool FPCGExPartitionByValuesBaseElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPartitionByValuesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PartitionByValuesBase)
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
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any partitions."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExPartitionByValuesBase
{
#pragma region FProcessor

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->bSplitOutput ? PCGExData::EIOInit::NoInit : PCGExData::EIOInit::Duplicate)

		Rules.Empty();
		const int32 NumPoints = PointDataFacade->GetNum();

		if (Settings->bWriteKeySum && !Settings->bSplitOutput)
		{
			PCGExArrayHelpers::InitArray(KeySums, NumPoints);
		}

		PCGExArrayHelpers::ArrayOfIndices(SortedIndices, NumPoints);

		FName Consumable = NAME_None;

		for (FPCGExPartitonRuleConfig& Config : Context->RulesConfigs)
		{
			const TSharedPtr<PCGExData::TBuffer<double>> DataCache = PointDataFacade->GetBroadcaster<double>(Config.Selector, true);
			if (!DataCache)
			{
				continue;
			}

			if (PCGExMetaHelpers::TryGetAttributeName(Config.Selector, PointDataFacade->Source->GetIn(), Consumable))
			{
				Context->AddConsumableAttributeName(Consumable);
			}
			
			if (FMath::IsNearlyZero(Config.FilterSize))
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Filter size must not be nearly equal to zero, replaced with 1. This will not produce the expected result."));
				Config.FilterSize = 1.0;
			}

			PCGExPartition::FRule& NewRule = Rules.Emplace_GetRef(Config);
			NewRule.DataCache = DataCache;
		}

		// Prepare each rule so it can cache the filter key by index
		for (PCGExPartition::FRule& Rule : Rules)
		{
			Rule.FilteredValues.SetNumZeroed(NumPoints);
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::PartitionByValues::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		// Lock-free parallel computation of partition keys
		// Each thread writes to unique indices in FilteredValues arrays
		PCGEX_SCOPE_LOOP(Index)
		{
			for (PCGExPartition::FRule& Rule : Rules)
			{
				Rule.FilteredValues[Index] = Rule.Filter(Index);
			}
		}
	}

	bool FProcessor::KeysChanged(const int32 IndexA, const int32 IndexB) const
	{
		for (const PCGExPartition::FRule& Rule : Rules)
		{
			if (Rule.FilteredValues[IndexA] != Rule.FilteredValues[IndexB])
			{
				return true;
			}
		}
		return false;
	}

	void FProcessor::BuildKeyToPartitionIndexMaps()
	{
		// Build per-rule key-to-partition-index maps for rules that need them
		for (PCGExPartition::FRule& Rule : Rules)
		{
			if (!Rule.RuleConfig->bWriteKey && !Rule.RuleConfig->bWriteTag)
			{
				continue;
			}
			if (!Rule.RuleConfig->bUsePartitionIndexAsKey && !Rule.RuleConfig->bTagUsePartitionIndexAsKey)
			{
				continue;
			}

			// Build key-to-index map for this rule based on sorted order
			TMap<int64, int32> KeyToIndex;
			int32 NextIndex = 0;
			for (const int32 SortedIdx : SortedIndices)
			{
				const int64 Key = Rule.FilteredValues[SortedIdx];
				if (!KeyToIndex.Contains(Key))
				{
					KeyToIndex.Add(Key, NextIndex++);
				}
			}
			Rule.KeyToPartitionIndex = MoveTemp(KeyToIndex);
		}
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const PCGExPartition::FPartitionRange& Range = PartitionRanges[Index];

			const TSharedRef<PCGExData::FPointIO> PartitionIO = Context->MainPoints->Pairs[Range.IOIndex].ToSharedRef();

			// Get point indices for this partition as array view
			TArrayView<const int32> PointIndices = MakeArrayView(&SortedIndices[Range.Start], Range.Count);

			PCGExPointArrayDataHelpers::SetNumPointsAllocated(PartitionIO->GetOut(), Range.Count, PartitionIO->GetAllocations());
			PartitionIO->InheritProperties(PointIndices, EPCGPointNativeProperties::All);

			// Force creation of valid keys once
			PartitionIO->GetOutKeys(true);

			// Get representative point for this partition (first point in range)
			const int32 RepresentativePointIdx = SortedIndices[Range.Start];

			// Write keys and tags for each rule
			int64 Sum = 0;
			for (const PCGExPartition::FRule& Rule : Rules)
			{
				const int64 PartitionKey = Rule.FilteredValues[RepresentativePointIdx];
				Sum += PartitionKey;

				if (Rule.RuleConfig->bWriteKey)
				{
					int64 WriteValue = PartitionKey;
					if (Rule.RuleConfig->bUsePartitionIndexAsKey)
					{
						const int32* PartitionIdx = Rule.KeyToPartitionIndex.Find(PartitionKey);
						WriteValue = PartitionIdx ? *PartitionIdx : 0;
					}
					PCGExData::WriteMark<int64>(PartitionIO, Rule.RuleConfig->KeyAttributeName, WriteValue);
				}

				if (Rule.RuleConfig->bWriteTag)
				{
					int64 TagValue = PartitionKey;
					if (Rule.RuleConfig->bTagUsePartitionIndexAsKey)
					{
						const int32* PartitionIdx = Rule.KeyToPartitionIndex.Find(PartitionKey);
						TagValue = PartitionIdx ? *PartitionIdx : 0;
					}
					PartitionIO->Tags->Set<int64>(Rule.RuleConfig->TagPrefixName.ToString(), TagValue);
				}
			}

			if (Settings->bWriteKeySum)
			{
				PCGExData::WriteMark<int64>(PartitionIO, Settings->KeySumAttributeName, Sum);
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		IProcessor::CompleteWork();

		if (Settings->bSplitOutput)
		{
			const int32 NumPoints = SortedIndices.Num();

			// Sort indices by lexicographic comparison of keys across all rules
			SortedIndices.Sort(
				[this](const int32 A, const int32 B)
				{
					for (const PCGExPartition::FRule& Rule : Rules)
					{
						const int64 KeyA = Rule.FilteredValues[A];
						const int64 KeyB = Rule.FilteredValues[B];
						if (KeyA != KeyB)
						{
							return KeyA < KeyB;
						}
					}
					return A < B; // Stable tiebreaker by original index
				});

			// Scan for partition boundaries
			PartitionRanges.Empty();
			if (NumPoints > 0)
			{
				int32 CurrentStart = 0;
				for (int32 i = 1; i < NumPoints; i++)
				{
					if (KeysChanged(SortedIndices[i - 1], SortedIndices[i]))
					{
						PartitionRanges.Emplace(CurrentStart, i - CurrentStart);
						CurrentStart = i;
					}
				}
				// Add last partition
				PartitionRanges.Emplace(CurrentStart, NumPoints - CurrentStart);
			}

			// Build key-to-partition-index maps for rules that need them
			BuildKeyToPartitionIndexMaps();

			// Create output IOs for each partition
			const int32 InsertOffset = Context->MainPoints->Pairs.Num();
			for (int32 i = 0; i < PartitionRanges.Num(); i++)
			{
				PartitionRanges[i].IOIndex = InsertOffset + i;
				Context->MainPoints->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::Duplicate);
			}

			StartParallelLoopForRange(PartitionRanges.Num(), 64);
			return;
		}

		// Non-split mode: write attributes directly to points
		TMap<int64, int64> IndiceMap;
		for (PCGExPartition::FRule& Rule : Rules)
		{
			if (!Rule.RuleConfig->bWriteKey)
			{
				continue;
			}

			if (Rule.RuleConfig->bUsePartitionIndexAsKey)
			{
				IndiceMap.Empty(Rule.FilteredValues.Num());
				int64 PIndex = -1;
				for (int64& Value : Rule.FilteredValues)
				{
					const int64* FilterPtr = IndiceMap.Find(Value);
					if (!FilterPtr)
					{
						IndiceMap.Add(Value, ++PIndex);
						Value = PIndex;
					}
					else
					{
						Value = *FilterPtr;
					}
				}
				IndiceMap.Empty();
			}

			const TSharedPtr<PCGExData::TBuffer<int32>> KeyWriter = PointDataFacade->GetWritable(Rule.RuleConfig->KeyAttributeName, 0, true, PCGExData::EBufferInit::New);
			for (int i = 0; i < Rule.FilteredValues.Num(); i++)
			{
				KeyWriter->SetValue(i, Rule.FilteredValues[i]);
				if (Settings->bWriteKeySum)
				{
					KeySums[i] += Rule.FilteredValues[i];
				}
			}
		}

		if (Settings->bWriteKeySum)
		{
			const TSharedPtr<PCGExData::TBuffer<int32>> KeySumWriter = PointDataFacade->GetWritable(Settings->KeySumAttributeName, 0, true, PCGExData::EBufferInit::New);
			for (int i = 0; i < KeySums.Num(); i++)
			{
				KeySumWriter->SetValue(i, KeySums[i]);
			}
		}

		PointDataFacade->WriteFastest(TaskManager);
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
