// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExValencyPattern.h"

#include "PCGExValencyConnectorPattern.generated.h"

/**
 * A pair of connector type indices defining which types connect source to target.
 * SourceTypeIndex = connector type on the current entry's node.
 * TargetTypeIndex = connector type on the target entry's node.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorTypePair
{
	GENERATED_BODY()

	/** Wildcard type index: matches any connector type */
	static constexpr int32 AnyTypeIndex = -2;

	/** Connector type index on the source (current) node. AnyTypeIndex = wildcard. */
	UPROPERTY()
	int32 SourceTypeIndex = INDEX_NONE;

	/** Connector type index on the target (adjacent) node. AnyTypeIndex = wildcard. */
	UPROPERTY()
	int32 TargetTypeIndex = INDEX_NONE;

	bool IsValid() const
	{
		return (SourceTypeIndex >= 0 || SourceTypeIndex == AnyTypeIndex) &&
		       (TargetTypeIndex >= 0 || TargetTypeIndex == AnyTypeIndex);
	}

	bool IsSourceWildcard() const { return SourceTypeIndex == AnyTypeIndex; }
	bool IsTargetWildcard() const { return TargetTypeIndex == AnyTypeIndex; }

	bool operator==(const FPCGExConnectorTypePair& Other) const
	{
		return SourceTypeIndex == Other.SourceTypeIndex && TargetTypeIndex == Other.TargetTypeIndex;
	}
};

/**
 * Adjacency defined by connector type pairs instead of orbital indices.
 * Supports "different pairs per edge" — any type pair in the array satisfies this adjacency.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternAdjacency
{
	GENERATED_BODY()

	/** Index of the target entry in the pattern */
	UPROPERTY()
	int32 TargetEntryIndex = INDEX_NONE;

	/**
	 * Type pairs that satisfy this adjacency (OR semantics).
	 * Any matching pair makes this adjacency valid.
	 */
	UPROPERTY()
	TArray<FPCGExConnectorTypePair> TypePairs;

	bool IsValid() const { return TargetEntryIndex >= 0 && TypePairs.Num() > 0; }
};

/**
 * Compiled connector pattern entry — one position in the pattern topology.
 * Mirrors FPCGExValencyPatternEntryCompiled but with connector-type-aware adjacency.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternEntryCompiled
{
	GENERATED_BODY()

	/**
	 * Module indices that can match this entry.
	 * Empty = wildcard (matches any module).
	 * Populated at runtime by ResolveModuleNames() using BondingRules.
	 */
	UPROPERTY()
	TArray<int32> ModuleIndices;

	/**
	 * Module names from authored data (for runtime resolution to indices).
	 * Empty = wildcard (matches any module).
	 * Resolved to ModuleIndices when BondingRules become available.
	 */
	UPROPERTY()
	TArray<FName> ModuleNames;

	/** If true, this entry is consumed by the pattern; if false, constraint-only */
	UPROPERTY()
	bool bIsActive = true;

	/**
	 * Connector type bitmask for boundary constraint.
	 * Bit N set = connector type N must have NO edges (unclaimed/unused).
	 */
	UPROPERTY()
	int64 BoundaryConnectorMask = 0;

	/**
	 * Connector type bitmask for wildcard constraint.
	 * Bit N set = connector type N must have at least one edge.
	 */
	UPROPERTY()
	int64 WildcardConnectorMask = 0;

	/** Adjacencies to other pattern entries via connector type pairs */
	UPROPERTY()
	TArray<FPCGExConnectorPatternAdjacency> Adjacencies;

	/** Check if a module index matches this entry */
	bool MatchesModule(int32 ModuleIndex) const
	{
		if (ModuleIndices.IsEmpty()) { return true; }
		return ModuleIndices.Contains(ModuleIndex);
	}

	/** Check if this entry is a wildcard (matches any module) */
	bool IsWildcard() const { return ModuleIndices.IsEmpty(); }
};

/**
 * Compiled connector pattern for runtime matching.
 * Contains the pattern topology defined via connector types.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternCompiled
{
	GENERATED_BODY()

	/** Pattern entries (index 0 = root) */
	UPROPERTY()
	TArray<FPCGExConnectorPatternEntryCompiled> Entries;

	/** Pattern settings (reused from orbital patterns) */
	UPROPERTY()
	FPCGExValencyPatternSettingsCompiled Settings;

	/** Soft reference to the ConnectorSet defining available types */
	UPROPERTY()
	TSoftObjectPtr<UObject> ConnectorSetRef;

	/** Replacement asset for Collapse mode */
	UPROPERTY()
	TSoftObjectPtr<UObject> ReplacementAsset;

	/** Swap target module index for Swap mode (-1 = invalid/unresolved) */
	UPROPERTY()
	int32 SwapTargetModuleIndex = INDEX_NONE;

	/** Number of active entries (entries that consume points) */
	UPROPERTY()
	int32 ActiveEntryCount = 0;

	/** Transform of the pattern root at compile time */
	UPROPERTY()
	FTransform RootTransform = FTransform::Identity;

	int32 GetEntryCount() const { return Entries.Num(); }
	bool IsValid() const { return Entries.Num() > 0; }
};

/**
 * Compiled set of all connector patterns.
 * Organized for efficient runtime matching.
 */
USTRUCT()
struct PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternSetCompiled
{
	GENERATED_BODY()

	/** All compiled connector patterns */
	UPROPERTY()
	TArray<FPCGExConnectorPatternCompiled> Patterns;

	/** Indices of exclusive patterns (processed first, in order) */
	UPROPERTY()
	TArray<int32> ExclusivePatternIndices;

	/** Indices of additive patterns (processed after exclusive) */
	UPROPERTY()
	TArray<int32> AdditivePatternIndices;

	bool HasPatterns() const { return Patterns.Num() > 0; }
	int32 GetPatternCount() const { return Patterns.Num(); }

	/**
	 * Resolve module names to indices using BondingRules.
	 * Populates ModuleIndices in each entry from stored ModuleNames.
	 * @param RulesModuleNames Module names array from FPCGExValencyBondingRulesCompiled
	 */
	void ResolveModuleNames(const TArray<FName>& RulesModuleNames);
};
