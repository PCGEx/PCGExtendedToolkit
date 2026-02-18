// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPatternMatcherOperation.h"
#include "Core/PCGExValencyConnectorCache.h"
#include "Core/PCGExValencyConnectorPattern.h"

#include "PCGExConnectorPatternMatcher.generated.h"

class UPCGExConnectorPatternAsset;
class UPCGExValencyConnectorSet;
struct FPCGExValencyBondingRulesCompiled;

/**
 * Connector-type-aware pattern matcher allocations.
 * Builds and owns the FConnectorCache needed for matching.
 */
namespace PCGExPatternMatcher
{
	struct PCGEXELEMENTSVALENCY_API FConnectorMatcherAllocations : FMatcherAllocations
	{
		/** Connector cache (built during FinalizeAllocations) */
		TSharedPtr<PCGExValency::FConnectorCache> ConnectorCache;

		/** Compiled connector patterns (owned by the ConnectorPatternAsset) */
		const FPCGExConnectorPatternSetCompiled* ConnectorPatterns = nullptr;

		/** References needed for cache building */
		const FPCGExValencyBondingRulesCompiled* CompiledRules = nullptr;
		const UPCGExValencyConnectorSet* ConnectorSet = nullptr;
		int32 MaxTypes = 0;

		/** Edge connector attribute name for reading */
		FName EdgeConnectorAttributeName;

		/** ValencyEntry reader (shared from batch) */
		TSharedPtr<PCGExData::TBuffer<int64>> ValencyEntryReader;

		virtual void FinalizeAllocations(
			const TSharedPtr<PCGExClusters::FCluster>& Cluster,
			const TSharedRef<PCGExData::FFacade>& EdgeFacade) override;

		virtual void SetValencyEntryReader(const TSharedPtr<PCGExData::TBuffer<int64>>& InReader) override
		{
			ValencyEntryReader = InReader;
		}
	};
}

/**
 * Connector pattern matcher operation.
 * Performs DFS with backtracking using connector-type queries instead of orbital lookups.
 *
 * Key differences from FPCGExDefaultPatternMatcherOperation:
 * 1. Uses FConnectorCache instead of FOrbitalCache
 * 2. Iterates TypePairs x neighbors-at-type (multiple candidates per edge)
 * 3. Boundary check via connector type mask
 * 4. Multi-candidate backtracking for each type pair
 */
class PCGEXELEMENTSVALENCY_API FPCGExConnectorPatternMatcherOperation : public FPCGExPatternMatcherOperation
{
public:
	FPCGExConnectorPatternMatcherOperation() = default;
	virtual ~FPCGExConnectorPatternMatcherOperation() override = default;

	/** Overlap resolution strategy */
	EPCGExPatternOverlapResolution OverlapResolution = EPCGExPatternOverlapResolution::WeightBased;

	//~ Begin FPCGExPatternMatcherOperation Interface
	virtual PCGExPatternMatcher::FMatchResult Match() override;
	virtual void Annotate(
		const TSharedPtr<PCGExData::TBuffer<FName>>& PatternNameWriter,
		const TSharedPtr<PCGExData::TBuffer<int32>>& MatchIndexWriter) override;
	virtual void CollectAnnotatedNodes(TSet<int32>& OutAnnotatedNodes) const override;
	virtual const FPCGExValencyPatternSettingsCompiled* GetMatchPatternSettings(const FPCGExValencyPatternMatch& Match) const override;
	virtual bool IsMatchEntryActive(const FPCGExValencyPatternMatch& Match, int32 EntryIndex) const override;
	virtual int32 GetMatchSwapTarget(const FPCGExValencyPatternMatch& Match) const override;
	//~ End FPCGExPatternMatcherOperation Interface

protected:
	/** Get the connector cache from allocations */
	const PCGExValency::FConnectorCache* GetConnectorCache() const;

	/** Get connector patterns from allocations */
	const FPCGExConnectorPatternSetCompiled* GetConnectorPatterns() const;

	/** Find all matches for a single connector pattern */
	void FindMatchesForPattern(int32 PatternIndex, const FPCGExConnectorPatternCompiled& Pattern);

	/** Try to match a pattern starting from a specific node */
	bool TryMatchPatternFromNode(
		int32 PatternIndex,
		const FPCGExConnectorPatternCompiled& Pattern,
		int32 StartNodeIndex,
		FPCGExValencyPatternMatch& OutMatch);

	/** Recursive DFS matching helper for connector-type adjacencies */
	bool MatchEntryRecursive(
		const FPCGExConnectorPatternCompiled& Pattern,
		int32 EntryIndex,
		TArray<int32>& EntryToNode,
		TSet<int32>& UsedNodes);

	/** Resolve overlapping matches */
	void ResolveOverlaps();

	/** Claim nodes for exclusive matches */
	void ClaimMatchedNodes();

	/** Validate MinMatches constraints */
	void ValidateMinMatches(PCGExPatternMatcher::FMatchResult& OutResult);

	/** Track match counts per pattern */
	TMap<int32, int32> PatternMatchCounts;
};

/**
 * Connector pattern matcher factory.
 * Creates connector-type-aware pattern matchers from ConnectorPatternAsset data.
 *
 * The ConnectorPatternAsset must be loaded during Boot/PostBoot (game thread).
 * ResolveAsset() should be called to validate and cache the resolved pointer.
 */
UCLASS(DisplayName = "Connector Pattern Matcher", meta=(ToolTip = "Pattern matcher using connector type pairs instead of orbital indices.", PCGExNodeLibraryDoc="valency/valency-patterns/connector-pattern-matcher"))
class PCGEXELEMENTSVALENCY_API UPCGExConnectorPatternMatcherFactory : public UPCGExPatternMatcherFactory
{
	GENERATED_BODY()

public:
	
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;
	
	/** The connector pattern asset defining patterns to match */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TSoftObjectPtr<UPCGExConnectorPatternAsset> ConnectorPatternAsset;

	/** How to resolve overlapping pattern matches */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPatternOverlapResolution OverlapResolution = EPCGExPatternOverlapResolution::WeightBased;

	/**
	 * Resolve and validate the ConnectorPatternAsset.
	 * Must be called on game thread (during Boot/PostBoot).
	 * Sets ResolvedCompiledRules, ResolvedConnectorSet, and ResolvedConnectorPatterns.
	 * @param InContext
	 * @param InCompiledRules Compiled bonding rules for connector resolution
	 * @param InConnectorSet Connector set for type index lookups
	 * @param InEdgeConnectorAttrName Attribute name for edge connector data
	 * @return True if asset was resolved and validated successfully
	 */
	bool ResolveAsset(
		FPCGExContext* InContext,
		const FPCGExValencyBondingRulesCompiled* InCompiledRules,
		const UPCGExValencyConnectorSet* InConnectorSet, const FName& InEdgeConnectorAttrName);

	//~ Begin UPCGExPatternMatcherFactory Interface
	virtual TSharedPtr<FPCGExPatternMatcherOperation> CreateOperation() const override;
	virtual void RegisterPrimaryBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual TSharedPtr<PCGExPatternMatcher::FMatcherAllocations> CreateAllocations(const TSharedRef<PCGExData::FFacade>& VtxFacade) const override;
	//~ End UPCGExPatternMatcherFactory Interface
	
protected:
	/** Resolved references (set by ResolveAsset) */
	const FPCGExConnectorPatternSetCompiled* ResolvedConnectorPatterns = nullptr;
	const FPCGExValencyBondingRulesCompiled* ResolvedCompiledRules = nullptr;
	const UPCGExValencyConnectorSet* ResolvedConnectorSet = nullptr;
	FName ResolvedEdgeConnectorAttrName;

	/** Local copy with resolved module names (owned by factory) */
	FPCGExConnectorPatternSetCompiled ResolvedPatternsStorage;
	int32 ResolvedMaxTypes = 0;
};
