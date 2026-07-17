// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Utils/PCGExMergePoints.h"

#include "PCGExVersion.h"
#include "PCGExMatchingCommon.h"
#include "Data/PCGExDataTags.h"
#include "Details/PCGExMatchingDetails.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Sorting/PCGExPointSorter.h"
#include "Utils/PCGExPointIOMerger.h"

#define LOCTEXT_NAMESPACE "PCGExMergePointsElement"
#define PCGEX_NAMESPACE MergePoints

namespace PCGExMergePoints
{
	PCGEX_CTX_STATE(State_MergingData);
}

UPCGExMergePointsSettings::UPCGExMergePointsSettings()
{
	// Merge Points is the one node that promotes @Data attributes to the elements domain by default;
	// bDataDomainToElements is off everywhere else.
	CarryOverDetails.bDataDomainToElements = true;
}

void FPCGExMergeList::Merge(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, const FPCGExCarryOverDetails* InCarryOverDetails, const FPCGExNameFiltersDetails* InTagsToAttributes)
{
	if (IOs.IsEmpty())
	{
		return;
	}

	FPCGExMergePointsContext* Ctx = TaskManager->GetContext<FPCGExMergePointsContext>();
	const TSharedPtr<PCGExData::FPointIO> CompositeIO = Ctx->MainPoints->Emplace_GetRef(IOs[0], PCGExData::EIOInit::New);

	if (!CompositeIO)
	{
		return;
	}

	CompositeDataFacade = MakeShared<PCGExData::FFacade>(CompositeIO.ToSharedRef());

	Merger = MakeShared<FPCGExPointIOMerger>(CompositeDataFacade.ToSharedRef());
	Merger->Append(IOs);
	Merger->MergeAsync(TaskManager, InCarryOverDetails, nullptr, false, InTagsToAttributes);
}

void FPCGExMergeList::Write(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) const
{
	CompositeDataFacade->WriteFastest(TaskManager);
}

#if WITH_EDITOR
TArray<FText> UPCGExMergePointsSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Merge Points by Tag")};
}

void UPCGExMergePointsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		MatchingDetails.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExMergePointsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		MatchingDetails.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

FPCGElementPtr UPCGExMergePointsSettings::CreateElement() const
{
	return MakeShared<FPCGExMergePointsElement>();
}

TArray<FPCGPinProperties> UPCGExMergePointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(MatchingDetails, PinProperties);
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Normal);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExMergePointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(GetMainOutputPin(), "Merged outputs.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(MatchingDetails, PinProperties);
	return PinProperties;
}

bool FPCGExMergePointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(MergePoints)

	// Get facades from main input
	TArray<TSharedPtr<PCGExData::FFacade>> Facades;
	Facades.Reserve(Context->MainPoints->Num());
	for (const TSharedPtr<PCGExData::FPointIO>& PointIO : Context->MainPoints->Pairs)
	{
		Facades.Add(MakeShared<PCGExData::FFacade>(PointIO.ToSharedRef()));
	}

	TArray<FPCGExSortRuleConfig> RuleConfigs = PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules);

	if (!RuleConfigs.IsEmpty())
	{
		auto Sorter = MakeShared<PCGExSorting::FSorter>(RuleConfigs);
		Sorter->SortDirection = Settings->SortDirection;

		if (!Sorter->Init(InContext, Facades))
		{
			PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Error with sorting rules."))
			return false;
		}

		for (int i = 0; i < Facades.Num(); i++)
		{
			Facades[i]->Idx = i;
		}
		Facades.Sort([&](const TSharedPtr<PCGExData::FFacade>& A, const TSharedPtr<PCGExData::FFacade>& B)
		{
			return Sorter->SortData(A->Idx, B->Idx);
		});
	}

	Context->MatchingDetails = Settings->MatchingDetails;

	Context->CarryOverDetails = Settings->CarryOverDetails;
	Context->CarryOverDetails.Init();

	Context->bTagToAttributes = Settings->bTagToAttributes;
	Context->TagsToAttributes = Settings->TagsToAttributes;
	Context->TagsToAttributes.Init();

	// Initialize the data matcher
	Context->DataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	Context->DataMatcher->SetDetails(&Context->MatchingDetails);

	// Check if matching is actually enabled in settings, not just if init succeeded
	const bool bMatchingEnabled = Context->MatchingDetails.IsEnabled() &&
		Context->DataMatcher->Init(Context, Facades, false, PCGExMatching::Labels::SourceMatchRulesLabel);

	if (!bMatchingEnabled)
	{
		// No matching rules or disabled - treat all as one group
		Context->Partitions.SetNum(1);
		for (int32 i = 0; i < Facades.Num(); i++)
		{
			Context->Partitions[0].Add(i);
		}
	}
	else
	{
		// Use the matching system to create partitions
		PCGExMatching::Helpers::GetMatchingSourcePartitions(
			Context->DataMatcher, Facades, Context->Partitions,
			Settings->bExclusivePartitions, nullptr);

		// When matching is enabled, single-element partitions are "unmatched" items
		// (they only matched themselves, no other data passed the matching rules)
		if (Context->MatchingDetails.WantsUnmatchedSplit())
		{
			// Extract unmatched indices (single-element partitions)
			for (int32 i = Context->Partitions.Num() - 1; i >= 0; --i)
			{
				if (Context->Partitions[i].Num() == 1)
				{
					Context->UnmatchedIndices.Add(Context->Partitions[i][0]);
					Context->Partitions.RemoveAt(i);
				}
			}
		}
	}

	// Remove empty partitions
	Context->Partitions.RemoveAll([](const TArray<int32>& Partition)
	{
		return Partition.IsEmpty();
	});

	// Allow execution if we have partitions to merge OR unmatched items to forward
	if (Context->Partitions.IsEmpty() && Context->UnmatchedIndices.IsEmpty())
	{
		return Context->CancelExecution(TEXT("No valid partitions created."));
	}

	return true;
}

bool FPCGExMergePointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(MergePoints)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		// Route unmatched data to the unmatched output pin
		for (const int32 UnmatchedIndex : Context->UnmatchedIndices)
		{
			const TSharedPtr<PCGExData::FPointIO>& UnmatchedIO = Context->MainPoints->Pairs[UnmatchedIndex];
			UnmatchedIO->OutputPin = PCGExMatching::Labels::OutputUnmatchedLabel;
			UnmatchedIO->InitializeOutput(PCGExData::EIOInit::Forward);
		}

		// Build merge lists from partitions
		Context->MergeLists.Reserve(Context->Partitions.Num());

		for (const TArray<int32>& Partition : Context->Partitions)
		{
			if (Partition.IsEmpty())
			{
				continue;
			}

			// Single item partitions just forward (these are not unmatched, they were already processed above)
			if (Partition.Num() == 1)
			{
				const TSharedPtr<PCGExData::FPointIO>& SingleIO = Context->MainPoints->Pairs[Partition[0]];
				SingleIO->InitializeOutput(PCGExData::EIOInit::Forward);
				continue;
			}

			// Create merge list for this partition
			PCGEX_MAKE_SHARED(MergeList, FPCGExMergeList)
			MergeList->IOs.Reserve(Partition.Num());

			for (const int32 SourceIndex : Partition)
			{
				MergeList->IOs.Add(Context->MainPoints->Pairs[SourceIndex]);
			}

			Context->MergeLists.Add(MergeList);
		}

		// Start merging
		TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
		Context->SetState(PCGExMergePoints::State_MergingData);

		if (!Context->MergeLists.IsEmpty())
		{
			PCGEX_ASYNC_GROUP_CHKD_RET(TaskManager, MergeAsync, true)

			for (const TSharedPtr<FPCGExMergeList>& List : Context->MergeLists)
			{
				MergeAsync->AddSimpleCallback([List, TaskManager, Det = Context->CarryOverDetails, bT2A = Context->bTagToAttributes, TagDet = Context->TagsToAttributes]
				{
					List->Merge(TaskManager, &Det, PCGExDataFilter::ResolveOptional(bT2A, TagDet));
				});
			}

			MergeAsync->StartSimpleCallbacks();
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExMergePoints::State_MergingData)
	{
		Context->SetState(PCGExCommon::States::State_Writing);

		if (!Context->MergeLists.IsEmpty())
		{
			TSharedPtr<PCGExMT::FTaskManager> TaskManager = Context->GetTaskManager();
			PCGEX_ASYNC_SCHEDULING_SCOPE(TaskManager, true)
			for (const TSharedPtr<FPCGExMergeList>& List : Context->MergeLists)
			{
				List->Write(TaskManager);
			}
		}
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGExCommon::States::State_Writing)
	{
		Context->MainPoints->StageOutputs();
		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
