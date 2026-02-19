// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matchers/PCGExConnectorPatternMatcher.h"

#include "Core/PCGExConnectorPatternAsset.h"
#include "Core/PCGExValencyCommon.h"
#include "Core/PCGExValencyBondingRules.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Core/PCGExValencyMap.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExStreamingHelpers.h"

#pragma region FConnectorMatcherAllocations

void PCGExPatternMatcher::FConnectorMatcherAllocations::FinalizeAllocations(
	const TSharedPtr<PCGExClusters::FCluster>& Cluster,
	const TSharedRef<PCGExData::FFacade>& EdgeFacade)
{
	if (!CompiledRules || !ConnectorSet || MaxTypes <= 0 || !ValencyEntryReader)
	{
		return;
	}

	// Read edge connector attribute
	const TSharedPtr<PCGExData::TBuffer<int64>> EdgeConnectorReader =
		EdgeFacade->GetReadable<int64>(EdgeConnectorAttributeName);

	if (!EdgeConnectorReader)
	{
		return;
	}

	// Build the connector cache
	ConnectorCache = MakeShared<PCGExValency::FConnectorCache>();
	ConnectorCache->BuildFrom(
		Cluster,
		EdgeConnectorReader,
		ValencyEntryReader,
		CompiledRules,
		ConnectorSet,
		MaxTypes);
}

#pragma endregion

#pragma region FPCGExConnectorPatternMatcherOperation

const PCGExValency::FConnectorCache* FPCGExConnectorPatternMatcherOperation::GetConnectorCache() const
{
	if (const auto* ConnAlloc = static_cast<const PCGExPatternMatcher::FConnectorMatcherAllocations*>(Allocations.Get()))
	{
		return ConnAlloc->ConnectorCache.Get();
	}
	return nullptr;
}

const FPCGExConnectorPatternSetCompiled* FPCGExConnectorPatternMatcherOperation::GetConnectorPatterns() const
{
	if (const auto* ConnAlloc = static_cast<const PCGExPatternMatcher::FConnectorMatcherAllocations*>(Allocations.Get()))
	{
		return ConnAlloc->ConnectorPatterns;
	}
	return nullptr;
}

void FPCGExConnectorPatternMatcherOperation::Annotate(
	const TSharedPtr<PCGExData::TBuffer<FName>>& PatternNameWriter,
	const TSharedPtr<PCGExData::TBuffer<int32>>& MatchIndexWriter)
{
	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns) { return; }

	int32 MatchCounter = 0;

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		if (!Match.IsValid()) { continue; }

		// Skip unclaimed exclusive matches
		if (!Match.bClaimed)
		{
			const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
			if (Pattern.Settings.bExclusive) { continue; }
		}

		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];

		// Annotate all active entries in the match
		for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
		{
			const FPCGExConnectorPatternEntryCompiled& Entry = Pattern.Entries[EntryIdx];
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

void FPCGExConnectorPatternMatcherOperation::CollectAnnotatedNodes(TSet<int32>& OutAnnotatedNodes) const
{
	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns) { return; }

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		if (!Match.IsValid()) { continue; }

		// Skip unclaimed exclusive matches
		if (!Match.bClaimed)
		{
			const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
			if (Pattern.Settings.bExclusive) { continue; }
		}

		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
		for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
		{
			if (Pattern.Entries[EntryIdx].bIsActive)
			{
				OutAnnotatedNodes.Add(Match.EntryToNode[EntryIdx]);
			}
		}
	}
}

const FPCGExValencyPatternSettingsCompiled* FPCGExConnectorPatternMatcherOperation::GetMatchPatternSettings(const FPCGExValencyPatternMatch& Match) const
{
	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns || !ConnPatterns->Patterns.IsValidIndex(Match.PatternIndex)) { return nullptr; }
	return &ConnPatterns->Patterns[Match.PatternIndex].Settings;
}

bool FPCGExConnectorPatternMatcherOperation::IsMatchEntryActive(const FPCGExValencyPatternMatch& Match, int32 EntryIndex) const
{
	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns || !ConnPatterns->Patterns.IsValidIndex(Match.PatternIndex)) { return false; }
	const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
	return Pattern.Entries.IsValidIndex(EntryIndex) && Pattern.Entries[EntryIndex].bIsActive;
}

int32 FPCGExConnectorPatternMatcherOperation::GetMatchSwapTarget(const FPCGExValencyPatternMatch& Match) const
{
	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns || !ConnPatterns->Patterns.IsValidIndex(Match.PatternIndex)) { return INDEX_NONE; }
	return ConnPatterns->Patterns[Match.PatternIndex].SwapTargetModuleIndex;
}

PCGExPatternMatcher::FMatchResult FPCGExConnectorPatternMatcherOperation::Match()
{
	PCGExPatternMatcher::FMatchResult Result;

	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	const PCGExValency::FConnectorCache* Cache = GetConnectorCache();

	if (!ConnPatterns || !ConnPatterns->HasPatterns() || !Cache || !Cache->IsValid())
	{
		Result.bSuccess = true; // No patterns or cache = nothing to match, not an error
		return Result;
	}

	PatternMatchCounts.Reset();

	// Find matches for exclusive patterns first
	for (const int32 PatternIdx : ConnPatterns->ExclusivePatternIndices)
	{
		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[PatternIdx];
		FindMatchesForPattern(PatternIdx, Pattern);
	}

	// Then additive patterns
	for (const int32 PatternIdx : ConnPatterns->AdditivePatternIndices)
	{
		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[PatternIdx];
		FindMatchesForPattern(PatternIdx, Pattern);
	}

	ResolveOverlaps();
	ClaimMatchedNodes();
	ValidateMinMatches(Result);

	// Compute statistics
	TSet<int32> MatchedPatterns;
	TSet<int32> AnnotatedNodeSet;

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		if (!Match.bClaimed)
		{
			const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
			if (Pattern.Settings.bExclusive) { continue; }
		}

		MatchedPatterns.Add(Match.PatternIndex);

		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
		for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
		{
			if (Pattern.Entries[EntryIdx].bIsActive)
			{
				AnnotatedNodeSet.Add(Match.EntryToNode[EntryIdx]);
			}
		}
	}

	Result.PatternsMatched = MatchedPatterns.Num();
	Result.NodesAnnotated = AnnotatedNodeSet.Num();
	Result.bSuccess = true;

	return Result;
}

void FPCGExConnectorPatternMatcherOperation::FindMatchesForPattern(
	int32 PatternIndex,
	const FPCGExConnectorPatternCompiled& Pattern)
{
	if (!Pattern.IsValid()) { return; }

	const PCGExValency::FConnectorCache* Cache = GetConnectorCache();
	if (!Cache) { return; }

	const FPCGExConnectorPatternEntryCompiled& RootEntry = Pattern.Entries[0];
	const int32 MaxMatches = Pattern.Settings.MaxMatches;

	if (!PatternMatchCounts.Contains(PatternIndex))
	{
		PatternMatchCounts.Add(PatternIndex, 0);
	}

	for (int32 NodeIdx = 0; NodeIdx < NumNodes; ++NodeIdx)
	{
		// For non-exclusive (additive) patterns, enforce MaxMatches during collection.
		// For exclusive patterns, collect all candidates — MaxMatches is enforced during
		// claiming so overlap-discarded matches don't consume slots.
		if (!Pattern.Settings.bExclusive && MaxMatches >= 0 && PatternMatchCounts[PatternIndex] >= MaxMatches)
		{
			break;
		}

		if (Pattern.Settings.bExclusive && IsNodeClaimed(NodeIdx)) { continue; }

		// Check module match
		const int32 ModuleIndex = GetModuleIndex(NodeIdx);
		if (!RootEntry.MatchesModule(ModuleIndex)) { continue; }

		// Boundary check: types in BoundaryConnectorMask must NOT have any edges
		if (RootEntry.BoundaryConnectorMask != 0)
		{
			const int64 NodeTypeMask = Cache->GetConnectorTypeMask(NodeIdx);
			if ((NodeTypeMask & RootEntry.BoundaryConnectorMask) != 0)
			{
				continue; // A boundary connector type has edges - skip
			}
		}

		// Wildcard check: types in WildcardConnectorMask MUST have at least one edge
		if (RootEntry.WildcardConnectorMask != 0)
		{
			const int64 NodeTypeMask = Cache->GetConnectorTypeMask(NodeIdx);
			if ((NodeTypeMask & RootEntry.WildcardConnectorMask) != RootEntry.WildcardConnectorMask)
			{
				continue; // A wildcard connector type is missing - skip
			}
		}

		FPCGExValencyPatternMatch Match;
		if (TryMatchPatternFromNode(PatternIndex, Pattern, NodeIdx, Match))
		{
			Matches.Add(MoveTemp(Match));
			PatternMatchCounts[PatternIndex]++;
		}
	}
}

bool FPCGExConnectorPatternMatcherOperation::TryMatchPatternFromNode(
	int32 PatternIndex,
	const FPCGExConnectorPatternCompiled& Pattern,
	int32 StartNodeIndex,
	FPCGExValencyPatternMatch& OutMatch)
{
	const int32 NumEntries = Pattern.GetEntryCount();

	TArray<int32> EntryToNode;
	EntryToNode.SetNum(NumEntries);
	for (int32 i = 0; i < NumEntries; ++i) { EntryToNode[i] = -1; }

	EntryToNode[0] = StartNodeIndex;

	TSet<int32> UsedNodes;
	UsedNodes.Add(StartNodeIndex);

	if (!MatchEntryRecursive(Pattern, 0, EntryToNode, UsedNodes))
	{
		return false;
	}

	// Verify all entries were matched
	for (int32 i = 0; i < NumEntries; ++i)
	{
		if (EntryToNode[i] < 0) { return false; }
	}

	OutMatch.PatternIndex = PatternIndex;
	OutMatch.EntryToNode = MoveTemp(EntryToNode);
	OutMatch.bClaimed = false;

	return true;
}

bool FPCGExConnectorPatternMatcherOperation::MatchEntryRecursive(
	const FPCGExConnectorPatternCompiled& Pattern,
	int32 EntryIndex,
	TArray<int32>& EntryToNode,
	TSet<int32>& UsedNodes)
{
	const FPCGExConnectorPatternEntryCompiled& Entry = Pattern.Entries[EntryIndex];
	const int32 CurrentNode = EntryToNode[EntryIndex];
	const PCGExValency::FConnectorCache* Cache = GetConnectorCache();

	if (!Cache) { return false; }

	const int32 CacheMaxTypes = Cache->GetMaxTypes();

	// Process all adjacencies from this entry
	for (const FPCGExConnectorPatternAdjacency& Adj : Entry.Adjacencies)
	{
		const int32 TargetEntryIdx = Adj.TargetEntryIndex;

		// If target entry is already matched, verify connectivity
		if (EntryToNode[TargetEntryIdx] >= 0)
		{
			const int32 ExistingNode = EntryToNode[TargetEntryIdx];

			// Verify that at least one type pair connects current node to existing node
			bool bConnected = false;
			for (const FPCGExConnectorTypePair& Pair : Adj.TypePairs)
			{
				// Source type range: wildcard iterates all types, specific uses one
				const int32 FirstSrc = Pair.IsSourceWildcard() ? 0 : Pair.SourceTypeIndex;
				const int32 LastSrc = Pair.IsSourceWildcard() ? CacheMaxTypes - 1 : Pair.SourceTypeIndex;

				for (int32 SrcType = FirstSrc; SrcType <= LastSrc; ++SrcType)
				{
					if (Cache->GetNeighborsAtType(CurrentNode, SrcType).Contains(ExistingNode))
					{
						bConnected = true;
						break;
					}
				}
				if (bConnected) { break; }
			}

			if (!bConnected) { return false; }
			continue;
		}

		// Try each type pair to find a matching neighbor
		const FPCGExConnectorPatternEntryCompiled& TargetEntry = Pattern.Entries[TargetEntryIdx];
		bool bFoundMatch = false;

		for (const FPCGExConnectorTypePair& Pair : Adj.TypePairs)
		{
			const int32 FirstSrc = Pair.IsSourceWildcard() ? 0 : Pair.SourceTypeIndex;
			const int32 LastSrc = Pair.IsSourceWildcard() ? CacheMaxTypes - 1 : Pair.SourceTypeIndex;

			for (int32 SrcType = FirstSrc; SrcType <= LastSrc; ++SrcType)
			{
				TConstArrayView<int32> Neighbors = Cache->GetNeighborsAtType(CurrentNode, SrcType);

				for (const int32 NeighborNode : Neighbors)
				{
					// Skip already used nodes
					if (UsedNodes.Contains(NeighborNode)) { continue; }

					// Check if the neighbor's module matches the target entry
					const int32 NeighborModule = GetModuleIndex(NeighborNode);
					if (!TargetEntry.MatchesModule(NeighborModule)) { continue; }

					// Check boundary constraints for target entry
					if (TargetEntry.BoundaryConnectorMask != 0)
					{
						const int64 NeighborTypeMask = Cache->GetConnectorTypeMask(NeighborNode);
						if ((NeighborTypeMask & TargetEntry.BoundaryConnectorMask) != 0) { continue; }
					}

					// Check wildcard constraints for target entry
					if (TargetEntry.WildcardConnectorMask != 0)
					{
						const int64 NeighborTypeMask = Cache->GetConnectorTypeMask(NeighborNode);
						if ((NeighborTypeMask & TargetEntry.WildcardConnectorMask) != TargetEntry.WildcardConnectorMask) { continue; }
					}

					// Verify reverse: neighbor connects back to current node via target type
					const int32 FirstTgt = Pair.IsTargetWildcard() ? 0 : Pair.TargetTypeIndex;
					const int32 LastTgt = Pair.IsTargetWildcard() ? CacheMaxTypes - 1 : Pair.TargetTypeIndex;

					bool bReverseConnected = false;
					for (int32 TgtType = FirstTgt; TgtType <= LastTgt; ++TgtType)
					{
						if (Cache->GetNeighborsAtType(NeighborNode, TgtType).Contains(CurrentNode))
						{
							bReverseConnected = true;
							break;
						}
					}
					if (!bReverseConnected) { continue; }

					// Match found - assign and recurse
					EntryToNode[TargetEntryIdx] = NeighborNode;
					UsedNodes.Add(NeighborNode);

					if (MatchEntryRecursive(Pattern, TargetEntryIdx, EntryToNode, UsedNodes))
					{
						bFoundMatch = true;
						break;
					}

					// Backtrack
					EntryToNode[TargetEntryIdx] = -1;
					UsedNodes.Remove(NeighborNode);
				}

				if (bFoundMatch) { break; }
			}

			if (bFoundMatch) { break; }
		}

		if (!bFoundMatch) { return false; }
	}

	return true;
}

void FPCGExConnectorPatternMatcherOperation::ResolveOverlaps()
{
	if (Matches.IsEmpty()) { return; }

	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns) { return; }

	switch (OverlapResolution)
	{
	case EPCGExPatternOverlapResolution::WeightBased:
		Matches.Sort([ConnPatterns](const FPCGExValencyPatternMatch& A, const FPCGExValencyPatternMatch& B)
		{
			return ConnPatterns->Patterns[A.PatternIndex].Settings.Weight >
				ConnPatterns->Patterns[B.PatternIndex].Settings.Weight;
		});
		break;

	case EPCGExPatternOverlapResolution::LargestFirst:
		Matches.Sort([](const FPCGExValencyPatternMatch& A, const FPCGExValencyPatternMatch& B)
		{
			return A.EntryToNode.Num() > B.EntryToNode.Num();
		});
		break;

	case EPCGExPatternOverlapResolution::SmallestFirst:
		Matches.Sort([](const FPCGExValencyPatternMatch& A, const FPCGExValencyPatternMatch& B)
		{
			return A.EntryToNode.Num() < B.EntryToNode.Num();
		});
		break;

	case EPCGExPatternOverlapResolution::FirstDefined:
		break;
	}
}

void FPCGExConnectorPatternMatcherOperation::ClaimMatchedNodes()
{
	if (!bExclusive) { return; }

	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns) { return; }

	// Track claimed count per pattern to enforce MaxMatches on effective (non-discarded) matches
	TMap<int32, int32> ClaimedCounts;

	for (FPCGExValencyPatternMatch& Match : Matches)
	{
		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];
		if (!Pattern.Settings.bExclusive) { continue; }

		// Enforce MaxMatches on claimed count (not candidate count)
		const int32 MaxMatches = Pattern.Settings.MaxMatches;
		int32& ClaimedCount = ClaimedCounts.FindOrAdd(Match.PatternIndex, 0);
		if (MaxMatches >= 0 && ClaimedCount >= MaxMatches) { continue; }

		bool bCanClaim = true;
		for (int32 EntryIdx = 0; EntryIdx < Pattern.Entries.Num(); ++EntryIdx)
		{
			if (!Pattern.Entries[EntryIdx].bIsActive) { continue; }

			const int32 NodeIdx = Match.EntryToNode[EntryIdx];
			if (IsNodeClaimed(NodeIdx))
			{
				bCanClaim = false;
				break;
			}
		}

		if (bCanClaim)
		{
			Match.bClaimed = true;
			ClaimedCount++;

			for (int32 EntryIdx = 0; EntryIdx < Pattern.Entries.Num(); ++EntryIdx)
			{
				if (!Pattern.Entries[EntryIdx].bIsActive) { continue; }
				ClaimNode(Match.EntryToNode[EntryIdx]);
			}
		}
	}
}

void FPCGExConnectorPatternMatcherOperation::ValidateMinMatches(PCGExPatternMatcher::FMatchResult& OutResult)
{
	const FPCGExConnectorPatternSetCompiled* ConnPatterns = GetConnectorPatterns();
	if (!ConnPatterns) { return; }

	TMap<int32, int32> EffectiveMatchCounts;

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[Match.PatternIndex];

		if (Pattern.Settings.bExclusive && !Match.bClaimed) { continue; }

		int32& Count = EffectiveMatchCounts.FindOrAdd(Match.PatternIndex, 0);
		Count++;
	}

	for (int32 PatternIdx = 0; PatternIdx < ConnPatterns->Patterns.Num(); ++PatternIdx)
	{
		const FPCGExConnectorPatternCompiled& Pattern = ConnPatterns->Patterns[PatternIdx];
		const int32 MinMatches = Pattern.Settings.MinMatches;
		const int32 MaxMatches = Pattern.Settings.MaxMatches;
		const int32 ActualCount = EffectiveMatchCounts.FindRef(PatternIdx);

		if (MinMatches > 0 && ActualCount < MinMatches)
		{
			OutResult.MinMatchViolations.Add(PatternIdx, ActualCount);
		}

		if (MaxMatches >= 0 && ActualCount >= MaxMatches)
		{
			OutResult.MaxMatchLimitReached.Add(PatternIdx);
		}
	}
}

void UPCGExConnectorPatternMatcherFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);

	if (const UPCGExConnectorPatternMatcherFactory* TypedOther = Cast<UPCGExConnectorPatternMatcherFactory>(Other))
	{
		ConnectorPatternAsset = TypedOther->ConnectorPatternAsset;
		OverlapResolution = TypedOther->OverlapResolution;
	}
}

#pragma endregion

#pragma region UPCGExConnectorPatternMatcherFactory

bool UPCGExConnectorPatternMatcherFactory::ResolveAsset(
	FPCGExContext* InContext,
	const FPCGExValencyBondingRulesCompiled* InCompiledRules,
	const UPCGExValencyConnectorSet* InConnectorSet, const FName& InEdgeConnectorAttrName)
{
	ResolvedCompiledRules = InCompiledRules;
	ResolvedConnectorSet = InConnectorSet;
	ResolvedEdgeConnectorAttrName = InEdgeConnectorAttrName;
	ResolvedMaxTypes = InConnectorSet ? InConnectorSet->Num() : 0;

	// Load the connector pattern asset
	PCGExHelpers::LoadBlocking_AnyThread(ConnectorPatternAsset.ToSoftObjectPath(), InContext);
	const UPCGExConnectorPatternAsset* PatternAsset = ConnectorPatternAsset.Get();
	if (!PatternAsset)
	{
		ResolvedConnectorPatterns = nullptr;
		return false;
	}

	const FPCGExConnectorPatternSetCompiled& AssetPatterns = PatternAsset->GetCompiledPatterns();
	if (!AssetPatterns.HasPatterns()) { return false; }

	// Copy compiled patterns and resolve module names using BondingRules
	ResolvedPatternsStorage = AssetPatterns;
	if (InCompiledRules)
	{
		ResolvedPatternsStorage.ResolveModuleNames(InCompiledRules->ModuleNames);
	}
	ResolvedConnectorPatterns = &ResolvedPatternsStorage;

	return true;
}

TSharedPtr<FPCGExPatternMatcherOperation> UPCGExConnectorPatternMatcherFactory::CreateOperation() const
{
	TSharedPtr<FPCGExConnectorPatternMatcherOperation> NewOperation = MakeShared<FPCGExConnectorPatternMatcherOperation>();

	InitOperation(NewOperation);
	NewOperation->OverlapResolution = OverlapResolution;

	return NewOperation;
}

void UPCGExConnectorPatternMatcherFactory::RegisterPrimaryBuffersDependencies(
	FPCGExContext* InContext,
	PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterPrimaryBuffersDependencies(InContext, FacadePreloader);

	// Register ValencyEntry attribute for vertex preloading (needed for module index lookup)
	// Edge connector attribute is read via EdgeFacade during FinalizeAllocations
}

TSharedPtr<PCGExPatternMatcher::FMatcherAllocations> UPCGExConnectorPatternMatcherFactory::CreateAllocations(
	const TSharedRef<PCGExData::FFacade>& VtxFacade) const
{
	if (!ResolvedConnectorPatterns || !ResolvedCompiledRules || !ResolvedConnectorSet)
	{
		return nullptr;
	}

	auto Alloc = MakeShared<PCGExPatternMatcher::FConnectorMatcherAllocations>();
	Alloc->ConnectorPatterns = ResolvedConnectorPatterns;
	Alloc->CompiledRules = ResolvedCompiledRules;
	Alloc->ConnectorSet = ResolvedConnectorSet;
	Alloc->MaxTypes = ResolvedMaxTypes;
	Alloc->EdgeConnectorAttributeName = ResolvedEdgeConnectorAttrName;

	// ValencyEntry reader will be set by the batch during preparation
	// (shared from batch's own ValencyEntryReader)

	return Alloc;
}

#pragma endregion
