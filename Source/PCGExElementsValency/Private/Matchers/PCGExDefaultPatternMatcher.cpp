// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matchers/PCGExDefaultPatternMatcher.h"

#include "Core/PCGExValencyCommon.h"

TSharedPtr<FPCGExPatternMatcherOperation> UPCGExDefaultPatternMatcher::CreateOperation() const
{
	PCGEX_FACTORY_NEW_OPERATION(DefaultPatternMatcherOperation)

	// Initialize common settings from base factory
	InitOperation(NewOperation);

	// Set default matcher-specific settings
	NewOperation->OverlapResolution = OverlapResolution;

	return NewOperation;
}

PCGExPatternMatcher::FMatchResult FPCGExDefaultPatternMatcherOperation::Match()
{
	PCGExPatternMatcher::FMatchResult Result;

	if (!CompiledPatterns || !CompiledPatterns->HasPatterns())
	{
		Result.bSuccess = true; // No patterns = nothing to match, but not an error
		return Result;
	}

	const FPCGExValencyPatternSetCompiled& PatternSet = *CompiledPatterns;

	// Reset match counts for this matching session
	PatternMatchCounts.Reset();

	// Find matches for exclusive patterns first
	for (const int32 PatternIdx : PatternSet.ExclusivePatternIndices)
	{
		// Apply filter if set
		if (PatternFilter && !PatternFilter(PatternIdx, CompiledPatterns)) { continue; }

		const FPCGExValencyPatternCompiled& Pattern = PatternSet.Patterns[PatternIdx];
		FindMatchesForPattern(PatternIdx, Pattern);
	}

	// Then find matches for additive patterns
	for (const int32 PatternIdx : PatternSet.AdditivePatternIndices)
	{
		// Apply filter if set
		if (PatternFilter && !PatternFilter(PatternIdx, CompiledPatterns)) { continue; }

		const FPCGExValencyPatternCompiled& Pattern = PatternSet.Patterns[PatternIdx];
		FindMatchesForPattern(PatternIdx, Pattern);
	}

	// Resolve overlaps
	ResolveOverlaps();

	// Claim nodes for exclusive matches
	ClaimMatchedNodes();

	// Validate MinMatches constraints (must be done after claiming to get accurate counts)
	ValidateMinMatches(Result);

	// Compute statistics
	TSet<int32> MatchedPatterns;
	TSet<int32> AnnotatedNodes;

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		if (!Match.bClaimed && CompiledPatterns->Patterns[Match.PatternIndex].Settings.bExclusive)
		{
			continue; // Skip unclaimed exclusive matches
		}

		MatchedPatterns.Add(Match.PatternIndex);

		const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];
		for (int32 EntryIdx = 0; EntryIdx < Match.EntryToNode.Num(); ++EntryIdx)
		{
			if (Pattern.Entries[EntryIdx].bIsActive)
			{
				AnnotatedNodes.Add(Match.EntryToNode[EntryIdx]);
			}
		}
	}

	Result.PatternsMatched = MatchedPatterns.Num();
	Result.NodesAnnotated = AnnotatedNodes.Num();
	Result.bSuccess = true;

	return Result;
}

void FPCGExDefaultPatternMatcherOperation::FindMatchesForPattern(int32 PatternIndex, const FPCGExValencyPatternCompiled& Pattern)
{
	if (!Pattern.IsValid()) { return; }

	const FPCGExValencyPatternEntryCompiled& RootEntry = Pattern.Entries[0];

	// Get MaxMatches constraint (-1 = unlimited)
	const int32 MaxMatches = Pattern.Settings.MaxMatches;

	// Initialize match count for this pattern if not present
	if (!PatternMatchCounts.Contains(PatternIndex))
	{
		PatternMatchCounts.Add(PatternIndex, 0);
	}

	// Try to match starting from each node that could match the root entry
	for (int32 NodeIdx = 0; NodeIdx < NumNodes; ++NodeIdx)
	{
		// For non-exclusive (additive) patterns, enforce MaxMatches during collection.
		// For exclusive patterns, collect all candidates — MaxMatches is enforced during
		// claiming so overlap-discarded matches don't consume slots.
		if (!Pattern.Settings.bExclusive && MaxMatches >= 0 && PatternMatchCounts[PatternIndex] >= MaxMatches)
		{
			break;
		}

		// Skip already claimed nodes for exclusive patterns
		if (Pattern.Settings.bExclusive && IsNodeClaimed(NodeIdx)) { continue; }

		// Check if this node's module matches the root entry
		const int32 ModuleIndex = GetModuleIndex(NodeIdx);
		if (!RootEntry.MatchesModule(ModuleIndex)) { continue; }

		// Check boundary constraints for root entry
		// BoundaryOrbitalMask indicates orbitals that MUST be empty (no neighbor)
		if (RootEntry.BoundaryOrbitalMask != 0)
		{
			const int64 NodeOccupiedMask = GetOrbitalMask(NodeIdx);
			if ((NodeOccupiedMask & RootEntry.BoundaryOrbitalMask) != 0)
			{
				continue; // Boundary orbital has a neighbor, skip
			}
		}

		// Check wildcard constraints for root entry
		// WildcardOrbitalMask indicates orbitals that MUST have a neighbor (any)
		if (RootEntry.WildcardOrbitalMask != 0)
		{
			const int64 NodeOccupiedMask = GetOrbitalMask(NodeIdx);
			if ((NodeOccupiedMask & RootEntry.WildcardOrbitalMask) != RootEntry.WildcardOrbitalMask)
			{
				continue; // Wildcard orbital missing a neighbor, skip
			}
		}

		// Try to match the full pattern starting from this node
		FPCGExValencyPatternMatch Match;
		if (TryMatchPatternFromNode(PatternIndex, Pattern, NodeIdx, Match))
		{
			Matches.Add(MoveTemp(Match));
			PatternMatchCounts[PatternIndex]++;
		}
	}
}

bool FPCGExDefaultPatternMatcherOperation::TryMatchPatternFromNode(
	int32 PatternIndex,
	const FPCGExValencyPatternCompiled& Pattern,
	int32 StartNodeIndex,
	FPCGExValencyPatternMatch& OutMatch)
{
	const int32 NumEntries = Pattern.GetEntryCount();

	// Initialize mapping
	TArray<int32> EntryToNode;
	EntryToNode.SetNum(NumEntries);
	for (int32 i = 0; i < NumEntries; ++i)
	{
		EntryToNode[i] = -1;
	}

	// Start with root entry mapped to start node
	EntryToNode[0] = StartNodeIndex;

	TSet<int32> UsedNodes;
	UsedNodes.Add(StartNodeIndex);

	// DFS to match remaining entries
	if (!MatchEntryRecursive(Pattern, 0, EntryToNode, UsedNodes))
	{
		return false;
	}

	// Verify all entries were matched
	for (int32 i = 0; i < NumEntries; ++i)
	{
		if (EntryToNode[i] < 0)
		{
			return false;
		}
	}

	// Build the match result
	OutMatch.PatternIndex = PatternIndex;
	OutMatch.EntryToNode = MoveTemp(EntryToNode);
	OutMatch.bClaimed = false;

	return true;
}

bool FPCGExDefaultPatternMatcherOperation::MatchEntryRecursive(
	const FPCGExValencyPatternCompiled& Pattern,
	int32 EntryIndex,
	TArray<int32>& EntryToNode,
	TSet<int32>& UsedNodes)
{
	const FPCGExValencyPatternEntryCompiled& Entry = Pattern.Entries[EntryIndex];
	const int32 CurrentNode = EntryToNode[EntryIndex];

	// Process all adjacencies from this entry
	for (const FIntVector& Adj : Entry.Adjacency)
	{
		const int32 TargetEntryIdx = Adj.X;
		const int32 SourceOrbital = Adj.Y;
		const int32 TargetOrbital = Adj.Z;

		// Check if target entry is already matched
		if (EntryToNode[TargetEntryIdx] >= 0)
		{
			// Verify the existing match is correct
			const int32 ExistingNode = EntryToNode[TargetEntryIdx];
			const int32 NeighborAtOrbital = GetNeighborAtOrbital(CurrentNode, SourceOrbital);
			if (NeighborAtOrbital != ExistingNode)
			{
				return false; // Inconsistent match
			}
			continue;
		}

		// Find the neighbor at the specified orbital
		const int32 NeighborNode = GetNeighborAtOrbital(CurrentNode, SourceOrbital);
		if (NeighborNode < 0)
		{
			return false; // No neighbor at required orbital
		}

		// Check if this node is already used by another entry
		if (UsedNodes.Contains(NeighborNode))
		{
			return false; // Node already used (patterns shouldn't have cycles to same node)
		}

		// Check if the neighbor's module matches the target entry
		const FPCGExValencyPatternEntryCompiled& TargetEntry = Pattern.Entries[TargetEntryIdx];
		const int32 NeighborModule = GetModuleIndex(NeighborNode);
		if (!TargetEntry.MatchesModule(NeighborModule))
		{
			return false;
		}

		// Check boundary constraints for target entry
		if (TargetEntry.BoundaryOrbitalMask != 0)
		{
			const int64 NeighborOccupiedMask = GetOrbitalMask(NeighborNode);
			if ((NeighborOccupiedMask & TargetEntry.BoundaryOrbitalMask) != 0)
			{
				return false; // Boundary orbital has a neighbor
			}
		}

		// Check wildcard constraints for target entry
		if (TargetEntry.WildcardOrbitalMask != 0)
		{
			const int64 NeighborOccupiedMask = GetOrbitalMask(NeighborNode);
			if ((NeighborOccupiedMask & TargetEntry.WildcardOrbitalMask) != TargetEntry.WildcardOrbitalMask)
			{
				return false; // Wildcard orbital missing a neighbor
			}
		}

		// Verify the reverse connection (neighbor connects back via expected orbital)
		const int32 ReverseNeighbor = GetNeighborAtOrbital(NeighborNode, TargetOrbital);
		if (ReverseNeighbor != CurrentNode)
		{
			return false; // Orbital connection is not bidirectional as expected
		}

		// Match found - assign and recurse
		EntryToNode[TargetEntryIdx] = NeighborNode;
		UsedNodes.Add(NeighborNode);

		// Recursively match from the new entry
		if (!MatchEntryRecursive(Pattern, TargetEntryIdx, EntryToNode, UsedNodes))
		{
			// Backtrack
			EntryToNode[TargetEntryIdx] = -1;
			UsedNodes.Remove(NeighborNode);
			return false;
		}
	}

	return true;
}

void FPCGExDefaultPatternMatcherOperation::ResolveOverlaps()
{
	if (Matches.IsEmpty()) { return; }

	// Sort matches based on resolution strategy
	switch (OverlapResolution)
	{
	case EPCGExPatternOverlapResolution::WeightBased:
		Matches.Sort([this](const FPCGExValencyPatternMatch& A, const FPCGExValencyPatternMatch& B)
		{
			const float WeightA = CompiledPatterns->Patterns[A.PatternIndex].Settings.Weight;
			const float WeightB = CompiledPatterns->Patterns[B.PatternIndex].Settings.Weight;
			return WeightA > WeightB;
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
		// Keep original order
		break;
	}
}

void FPCGExDefaultPatternMatcherOperation::ClaimMatchedNodes()
{
	if (!bExclusive) { return; }

	// Track claimed count per pattern to enforce MaxMatches on effective (non-discarded) matches
	TMap<int32, int32> ClaimedCounts;

	// Claim nodes for exclusive patterns (in sorted/priority order)
	for (FPCGExValencyPatternMatch& Match : Matches)
	{
		const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];
		if (!Pattern.Settings.bExclusive) { continue; }

		// Enforce MaxMatches on claimed count (not candidate count)
		const int32 MaxMatches = Pattern.Settings.MaxMatches;
		int32& ClaimedCount = ClaimedCounts.FindOrAdd(Match.PatternIndex, 0);
		if (MaxMatches >= 0 && ClaimedCount >= MaxMatches) { continue; }

		// Check if any active nodes are already claimed
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

			// Claim all active nodes
			for (int32 EntryIdx = 0; EntryIdx < Pattern.Entries.Num(); ++EntryIdx)
			{
				if (!Pattern.Entries[EntryIdx].bIsActive) { continue; }
				ClaimNode(Match.EntryToNode[EntryIdx]);
			}
		}
	}
}

void FPCGExDefaultPatternMatcherOperation::ValidateMinMatches(PCGExPatternMatcher::FMatchResult& OutResult)
{
	if (!CompiledPatterns) { return; }

	// Count effective matches per pattern (only claimed matches count for exclusive patterns)
	TMap<int32, int32> EffectiveMatchCounts;

	for (const FPCGExValencyPatternMatch& Match : Matches)
	{
		const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[Match.PatternIndex];

		// For exclusive patterns, only count claimed matches
		if (Pattern.Settings.bExclusive && !Match.bClaimed)
		{
			continue;
		}

		int32& Count = EffectiveMatchCounts.FindOrAdd(Match.PatternIndex, 0);
		Count++;
	}

	// Check each pattern's constraints
	for (int32 PatternIdx = 0; PatternIdx < CompiledPatterns->Patterns.Num(); ++PatternIdx)
	{
		// Skip patterns not considered by this matcher
		if (PatternFilter && !PatternFilter(PatternIdx, CompiledPatterns)) { continue; }

		const FPCGExValencyPatternCompiled& Pattern = CompiledPatterns->Patterns[PatternIdx];
		const int32 MinMatches = Pattern.Settings.MinMatches;
		const int32 MaxMatches = Pattern.Settings.MaxMatches;
		const int32 ActualCount = EffectiveMatchCounts.FindRef(PatternIdx);

		// Check MinMatches constraint
		if (MinMatches > 0 && ActualCount < MinMatches)
		{
			OutResult.MinMatchViolations.Add(PatternIdx, ActualCount);
		}

		// Track if MaxMatches was reached (informational)
		if (MaxMatches >= 0 && ActualCount >= MaxMatches)
		{
			OutResult.MaxMatchLimitReached.Add(PatternIdx);
		}
	}
}
