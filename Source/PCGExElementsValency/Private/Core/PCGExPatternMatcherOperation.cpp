// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPatternMatcherOperation.h"

#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyMap.h"
#include "Data/PCGExData.h"

void FPCGExPatternMatcherOperation::Initialize(
	const TSharedPtr<PCGExClusters::FCluster>& InCluster,
	const FPCGExValencyPatternSetCompiled* InCompiledPatterns,
	const PCGExValency::FOrbitalCache* InOrbitalCache,
	const TSharedPtr<PCGExData::TBuffer<int64>>& InValencyEntryReader,
	int32 InNumNodes,
	TSet<int32>* InClaimedNodes,
	int32 InSeed, const TSharedPtr<PCGExPatternMatcher::FMatcherAllocations>& InAllocations)
{
	Cluster = InCluster;
	CompiledPatterns = InCompiledPatterns;
	OrbitalCache = InOrbitalCache;
	ValencyEntryReader = InValencyEntryReader;
	NumNodes = InNumNodes;
	ClaimedNodes = InClaimedNodes;
	Allocations = InAllocations;

	RandomStream.Initialize(InSeed);
	Matches.Reset();
}

void FPCGExPatternMatcherOperation::Annotate(
	const TSharedPtr<PCGExData::TBuffer<FName>>& PatternNameWriter,
	const TSharedPtr<PCGExData::TBuffer<int32>>& MatchIndexWriter)
{
	if (!CompiledPatterns) { return; }

	int32 MatchCounter = 0;

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		if (!Match.IsValid()) { continue; }

		// Skip unclaimed exclusive matches
		if (!Match.bClaimed)
		{
			const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];
			if (Pattern.Settings.bExclusive) { continue; }
		}

		const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];

		// Annotate all active entries in the match
		for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
		{
			const FPCGExValencyPatternEntryCompiled& Entry = Pattern.Entries[EntryIdx];
			if (!Entry.bIsActive) { continue; }

			const int32 NodeIdx = Match.EntryToNode[EntryIdx];
			const int32 PointIdx = GetPointIndex(NodeIdx);
			if (PointIdx < 0) { continue; }

			if (PatternNameWriter)
			{
				PatternNameWriter->SetValue(PointIdx, Pattern.Settings.PatternName);
			}

			if (MatchIndexWriter)
			{
				MatchIndexWriter->SetValue(PointIdx, MatchCounter);
			}
		}

		++MatchCounter;
	}
}

void FPCGExPatternMatcherOperation::CollectAnnotatedNodes(TSet<int32>& OutAnnotatedNodes) const
{
	if (!CompiledPatterns) { return; }

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		if (!Match.IsValid()) { continue; }

		// Skip unclaimed exclusive matches
		if (!Match.bClaimed)
		{
			const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];
			if (Pattern.Settings.bExclusive) { continue; }
		}

		const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];
		for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
		{
			if (Pattern.Entries[EntryIdx].bIsActive)
			{
				OutAnnotatedNodes.Add(Match.EntryToNode[EntryIdx]);
			}
		}
	}
}

const FPCGExValencyPatternSettingsCompiled* FPCGExPatternMatcherOperation::GetMatchPatternSettings(const FPCGExValencyPatternMatch& Match) const
{
	if (!CompiledPatterns || !CompiledPatterns->Patterns.IsValidIndex(Match.PatternIndex)) { return nullptr; }
	return &CompiledPatterns->Patterns[Match.PatternIndex].Settings;
}

bool FPCGExPatternMatcherOperation::IsMatchEntryActive(const FPCGExValencyPatternMatch& Match, int32 EntryIndex) const
{
	if (!CompiledPatterns || !CompiledPatterns->Patterns.IsValidIndex(Match.PatternIndex)) { return false; }
	const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];
	return Pattern.Entries.IsValidIndex(EntryIndex) && Pattern.Entries[EntryIndex].bIsActive;
}

int32 FPCGExPatternMatcherOperation::GetMatchSwapTarget(const FPCGExValencyPatternMatch& Match) const
{
	if (!CompiledPatterns || !CompiledPatterns->Patterns.IsValidIndex(Match.PatternIndex)) { return INDEX_NONE; }
	return CompiledPatterns->Patterns[Match.PatternIndex].SwapTargetModuleIndex;
}

int32 FPCGExPatternMatcherOperation::GetModuleIndex(int32 NodeIndex) const
{
	if (!ValencyEntryReader) { return -1; }
	const int32 PointIndex = GetPointIndex(NodeIndex);
	if (PointIndex < 0) { return -1; }
	const uint64 Hash = static_cast<uint64>(ValencyEntryReader->Read(PointIndex));
	if (Hash == PCGExValency::EntryData::INVALID_ENTRY) { return -1; }
	return PCGExValency::EntryData::GetModuleIndex(Hash);
}

void UPCGExPatternMatcherFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);

	if (const UPCGExPatternMatcherFactory* TypedOther = Cast<UPCGExPatternMatcherFactory>(Other))
	{
		RequiredTags = TypedOther->RequiredTags;
		ExcludedTags = TypedOther->ExcludedTags;
		PatternNames = TypedOther->PatternNames;
		bExclusive = TypedOther->bExclusive;
	}
}

void UPCGExPatternMatcherFactory::RegisterPrimaryBuffersDependencies(
	FPCGExContext* InContext,
	PCGExData::FFacadePreloader& FacadePreloader) const
{
	// Base implementation does nothing - derived classes override to declare attributes
}

TSharedPtr<PCGExPatternMatcher::FMatcherAllocations> UPCGExPatternMatcherFactory::CreateAllocations(
	const TSharedRef<PCGExData::FFacade>& VtxFacade) const
{
	// Base implementation returns nullptr - derived classes override if they need allocations
	return nullptr;
}

bool UPCGExPatternMatcherFactory::PassesPatternFilter(
	const FPCGExValencyPatternCompiled& Pattern,
	const TArray<FName>& PatternTags) const
{
	// Check pattern name filter
	if (!PatternNames.IsEmpty())
	{
		if (!PatternNames.Contains(Pattern.Settings.PatternName))
		{
			return false;
		}
	}

	// Check required tags (pattern must have ALL required tags)
	if (!RequiredTags.IsEmpty())
	{
		for (const FName& RequiredTag : RequiredTags)
		{
			if (!PatternTags.Contains(RequiredTag))
			{
				return false;
			}
		}
	}

	// Check excluded tags (pattern must have NONE of the excluded tags)
	if (!ExcludedTags.IsEmpty())
	{
		for (const FName& ExcludedTag : ExcludedTags)
		{
			if (PatternTags.Contains(ExcludedTag))
			{
				return false;
			}
		}
	}

	return true;
}

void UPCGExPatternMatcherFactory::InitOperation(const TSharedPtr<FPCGExPatternMatcherOperation>& Operation) const
{
	if (!Operation) { return; }

	// Set common properties
	Operation->bExclusive = bExclusive;

	// Set up pattern filter using factory's filter settings
	// Note: PatternTags will need to come from compiled data when available
	Operation->PatternFilter = [this](int32 PatternIndex, const FPCGExValencyPatternSetCompiled* Patterns) -> bool
	{
		if (!Patterns || PatternIndex < 0 || PatternIndex >= Patterns->Patterns.Num())
		{
			return false;
		}

		const FPCGExValencyPatternCompiled& Pattern = Patterns->Patterns[PatternIndex];

		return PassesPatternFilter(Pattern, Pattern.Settings.Tags);
	};
}
