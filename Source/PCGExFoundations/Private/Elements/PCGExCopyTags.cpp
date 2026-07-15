// Copyright 2026 Timothe Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExCopyTags.h"

#include "PCGExVersion.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGModule.h"
#include "PCGExCommon.h"
#include "PCGExMatchingCommon.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExDataValue.h"
#include "Data/PCGExTaggedData.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExCopyTagsElement"
#define PCGEX_NAMESPACE CopyTags

#pragma region UPCGExCopyTagsSettings

TArray<FPCGPinProperties> UPCGExCopyTagsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGExCommon::Labels::SourceSourcesLabel, "Data that receives tags. Forwarded to the output untouched apart from the added tags.", Required)
	PCGEX_PIN_ANY(PCGExCommon::Labels::SourceTargetsLabel, "Reference data that provides tags. Not forwarded to the output.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExCopyTagsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGExCommon::Labels::SourceSourcesLabel, "The Source data, forwarded with the copied tags added.", Required)

	// Mirror PCGExMatching::Helpers::DeclareMatchingRulesOutputs (Default usage), but declare the
	// Unmatched pin as Any rather than Point -- the helper hard-codes Point, which would drop non-point
	// Sources routed here. Status follows the helper: Normal when splitting is on, Advanced otherwise.
	{
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(PCGExMatching::Labels::OutputUnmatchedLabel, EPCGDataType::Any);
		PCGEX_PIN_TOOLTIP("Source data that matched no Target.")
		Pin.PinStatus = DataMatching.WantsUnmatchedSplit() ? EPCGPinStatus::Normal : EPCGPinStatus::Advanced;
	}

	return PinProperties;
}

FPCGElementPtr UPCGExCopyTagsSettings::CreateElement() const
{
	return MakeShared<FPCGExCopyTagsElement>();
}

#if WITH_EDITOR
void UPCGExCopyTagsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.RenamePins(this, InOutNode);
	}
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCopyTagsSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		DataMatching.ApplyDeprecation();
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

#pragma endregion

namespace PCGExCopyTags
{
	FORCEINLINE bool IsProtectedTag(const FString& Key)
	{
		return Key == PCGExClusters::Labels::TagStr_PCGExVtx
			|| Key == PCGExClusters::Labels::TagStr_PCGExEdges
			|| Key == PCGExClusters::Labels::TagStr_PCGExCluster;
	}

	// Adds Flattened to OutTags only when the destination doesn't already carry Key (missing-only),
	// the key isn't a protected cluster tag, and it passes the name filter. SeenKeys tracks keys
	// already present/added so the first matched Target wins on conflicts.
	FORCEINLINE void TryCopyTag(
		const FPCGExCopyTagsContext* Context,
		const FString& Key,
		const FString& Flattened,
		TSet<FString>& SeenKeys,
		TSet<FString>& OutTags)
	{
		if (SeenKeys.Contains(Key))
		{
			return;
		}
		if (Context->bProtectClusterTags && IsProtectedTag(Key))
		{
			return;
		}
		if (!Context->TagFilters.Test(Key))
		{
			return;
		}

		OutTags.Add(Flattened);
		SeenKeys.Add(Key);
	}

	void CopyMissingTags(
		const FPCGExCopyTagsContext* Context,
		const TArray<int32>& MatchedTargets,
		TSet<FString>& SeenKeys,
		TSet<FString>& OutTags)
	{
		for (const int32 TargetIdx : MatchedTargets)
		{
			const TSharedPtr<PCGExData::FTags>& Tags = Context->TargetTags[TargetIdx];
			if (!Tags)
			{
				continue;
			}

			// Raw tags carry no value: the tag string is also its key.
			for (const FString& Raw : Tags->RawTags)
			{
				TryCopyTag(Context, Raw, Raw, SeenKeys, OutTags);
			}

			// Value tags: copy the flattened "Key:Value" form, keyed on Key.
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : Tags->ValueTags)
			{
				if (!Pair.Value)
				{
					continue;
				}
				TryCopyTag(Context, Pair.Key, Pair.Value->Flatten(Pair.Key), SeenKeys, OutTags);
			}
		}
	}
}

#pragma region FPCGExCopyTagsElement

bool FPCGExCopyTagsElement::Boot(FPCGExContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCopyTagsElement::Boot);

	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_SETTINGS_C(InContext, CopyTags)
	FPCGExCopyTagsContext* Context = static_cast<FPCGExCopyTagsContext*>(InContext);

	Context->bProtectClusterTags = Settings->bProtectClusterTags;
	Context->TagFilters = Settings->TagFilters;
	// Cluster identity is protected separately (bProtectClusterTags); let the name filter govern every
	// other tag uniformly -- including PCGEx-prefixed ones -- instead of silently passing them through.
	Context->TagFilters.bPreservePCGExData = false;
	Context->TagFilters.Init();

	// Gather Targets as raw data + parsed tags. Tags are stored on the Context so they outlive matching
	// -- FPCGExTaggedData only keeps a TWeakPtr to them. Duplicate data is skipped to stay index-aligned
	// with the matcher: FDataMatcher::RegisterTaggedData drops repeated UPCGData*, so keeping them here
	// would desync GetMatchingSourcesIndices results from TargetTags.
	const TArray<FPCGTaggedData> Targets = InContext->InputData.GetInputsByPin(PCGExCommon::Labels::SourceTargetsLabel);
	Context->TargetData.Reserve(Targets.Num());
	Context->TargetTags.Reserve(Targets.Num());
	TSet<const UPCGData*> SeenTargets;
	SeenTargets.Reserve(Targets.Num());
	for (const FPCGTaggedData& Tagged : Targets)
	{
		if (!Tagged.Data)
		{
			continue;
		}
		bool bAlreadySeen = false;
		SeenTargets.Add(Tagged.Data, &bAlreadySeen);
		if (bAlreadySeen)
		{
			continue;
		}
		Context->TargetData.Add(Tagged.Data);
		Context->TargetTags.Add(MakeShared<PCGExData::FTags>(Tagged.Tags));
	}

	if (Context->TargetData.IsEmpty())
	{
		// Nothing to copy from -- Sources are forwarded untouched in AdvanceWork.
		return true;
	}

	Context->DataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	Context->DataMatcher->SetDetails(&Settings->DataMatching);

	const bool bInit = Context->DataMatcher->Init(
		InContext, Context->TargetData, Context->TargetTags, true, PCGExMatching::Labels::SourceMatchRulesLabel);

	if (!bInit && Settings->DataMatching.IsEnabled())
	{
		// Init fails (matcher falls back to Disabled = every Source matches every Target) when matching
		// is enabled but couldn't be set up: no rules were provided, OR a connected rule failed to
		// resolve (e.g. a missing target attribute). Don't assume which -- just report the fallback.
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Data matching is enabled but could not be initialized (no valid rules, or a rule failed to resolve) -- falling back to copying tags from every Target onto every Source."));
	}

	return true;
}

bool FPCGExCopyTagsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCopyTagsElement::AdvanceWork);

	PCGEX_SETTINGS_C(InContext, CopyTags)
	FPCGExCopyTagsContext* Context = static_cast<FPCGExCopyTagsContext*>(InContext);

	const TArray<FPCGTaggedData> Sources = InContext->InputData.GetInputsByPin(PCGExCommon::Labels::SourceSourcesLabel);

	TArray<int32> ValidSources;
	ValidSources.Reserve(Sources.Num());
	for (int32 i = 0; i < Sources.Num(); i++)
	{
		if (Sources[i].Data)
		{
			ValidSources.Add(i);
		}
	}

	if (ValidSources.IsEmpty())
	{
		InContext->Done();
		return InContext->TryComplete();
	}

	const bool bHasTargets = !Context->TargetData.IsEmpty() && Context->DataMatcher.IsValid();
	const bool bSplitUnmatched = Settings->DataMatching.WantsUnmatchedSplit();
	const int32 NumTargets = Context->TargetData.Num();

	// One output per source, produced in parallel then appended single-threaded. Each task
	// writes only its own slot and its own (disjoint) output tag set, so no shared mutation.
	TArray<FPCGTaggedData> Results;
	Results.SetNum(ValidSources.Num());

	PCGExMT::ParallelOrSequential(
		ValidSources.Num(), [&](const int32 ValidIdx)
		{
			const int32 SourceIdx = ValidSources[ValidIdx];
			const FPCGTaggedData& InTagged = Sources[SourceIdx];

			FPCGTaggedData& OutTagged = Results[ValidIdx];
			OutTagged = InTagged; // shares the Data pointer, copies the tag set -- no data duplication
			OutTagged.Pin = PCGExCommon::Labels::SourceSourcesLabel;

			if (!bHasTargets)
			{
				return; // pure passthrough
			}

			const TSharedPtr<PCGExData::FTags> SourceTags = MakeShared<PCGExData::FTags>(InTagged.Tags);
			// No accessor keys needed: no match-rule operation reads the candidate's Keys (they use
			// Data, Index, or GetTags()), so building them per source would be wasted hot-path work.
			const FPCGExTaggedData Candidate(InTagged.Data, SourceIdx, SourceTags, nullptr);

			PCGExMatching::FScope Scope(NumTargets, true);
			TArray<int32> MatchedTargets;
			Context->DataMatcher->GetMatchingSourcesIndices(Candidate, Scope, MatchedTargets);

			if (MatchedTargets.IsEmpty())
			{
				if (bSplitUnmatched)
				{
					OutTagged.Pin = PCGExMatching::Labels::OutputUnmatchedLabel;
				}
				return;
			}

			// Seed SeenKeys with the Source's existing tag keys so we only copy what's missing.
			TSet<FString> SeenKeys;
			SeenKeys.Reserve(SourceTags->Num());
			for (const FString& Raw : SourceTags->RawTags)
			{
				SeenKeys.Add(Raw);
			}
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : SourceTags->ValueTags)
			{
				SeenKeys.Add(Pair.Key);
			}

			PCGExCopyTags::CopyMissingTags(Context, MatchedTargets, SeenKeys, OutTagged.Tags);
		},
		/*Threshold=*/8, EParallelForFlags::Unbalanced);

	InContext->OutputData.TaggedData.Reserve(InContext->OutputData.TaggedData.Num() + Results.Num());
	for (FPCGTaggedData& R : Results)
	{
		if (R.Data)
		{
			InContext->OutputData.TaggedData.Emplace(MoveTemp(R));
		}
	}

	InContext->Done();
	return InContext->TryComplete();
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
