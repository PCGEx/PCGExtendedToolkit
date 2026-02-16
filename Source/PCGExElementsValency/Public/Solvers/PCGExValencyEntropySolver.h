// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExValencySolverOperation.h"

#include "PCGExValencyEntropySolver.generated.h"

/**
 * WFC-specific per-state data (internal to entropy solver).
 * This is separate from FValencyState to keep solver-specific data isolated.
 */
struct FWFCStateData
{
	/** Valid candidate module indices (shrinks during propagation) */
	TArray<int32> Candidates;

	/** Cached entropy value for priority queue ordering */
	float Entropy = TNumericLimits<float>::Max();

	/** Ratio of resolved neighbors (higher = more constrained, used as tiebreaker) */
	float NeighborResolutionRatio = 0.0f;

	/** Reset state */
	void Reset()
	{
		Candidates.Empty();
		Entropy = TNumericLimits<float>::Max();
		NeighborResolutionRatio = 0.0f;
	}
};

/**
 * Entropy-based WFC solver.
 * Collapses states in order of lowest entropy (fewest candidates).
 * Uses neighbor resolution ratio as tiebreaker.
 */
class PCGEXELEMENTSVALENCY_API FPCGExValencyEntropySolver : public FPCGExValencySolverOperation
{
public:
	FPCGExValencyEntropySolver() = default;
	virtual ~FPCGExValencyEntropySolver() override = default;

	// MinimumSpawnWeightBoost inherited from base class

	virtual void Initialize(
		const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
		TArray<PCGExValency::FValencyState>& InValencyStates,
		const PCGExValency::FOrbitalCache* InOrbitalCache,
		int32 InSeed,
		const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations = nullptr) override;

	virtual PCGExValency::FSolveResult Solve() override;

protected:
	/** WFC-specific state per valency state (parallel to ValencyStates) */
	TArray<FWFCStateData> StateData;

	/** Priority queue of unresolved state indices, sorted by entropy */
	TArray<int32> EntropyQueue;

	/**
	 * Initialize candidates for all states based on orbital mask matching.
	 */
	void InitializeAllCandidates();

	/**
	 * Calculate entropy for a state.
	 * Entropy = candidate count - (tiebreaker based on neighbor resolution)
	 */
	void UpdateEntropy(int32 StateIndex);

	/**
	 * Rebuild the entropy queue from all unresolved states.
	 */
	void RebuildEntropyQueue();

	/**
	 * Pop the lowest entropy state from the queue.
	 * @return State index, or -1 if queue is empty
	 */
	int32 PopLowestEntropy();

	/**
	 * Collapse a state by selecting a module from its candidates.
	 * @param StateIndex The state to collapse
	 * @return True if successful, false if no valid candidates (contradiction)
	 */
	bool CollapseState(int32 StateIndex);

	/**
	 * Propagate constraints from a resolved state to its neighbors.
	 * Updates neighbor entropy values.
	 * @param ResolvedStateIndex The just-resolved state
	 */
	void PropagateConstraints(int32 ResolvedStateIndex);

	/**
	 * Filter candidates for a state based on resolved neighbor constraints.
	 * @param StateIndex The state to filter
	 * @return True if candidates remain, false if empty (contradiction)
	 */
	bool FilterCandidates(int32 StateIndex);

	/**
	 * Check if selecting a candidate would leave any unresolved neighbor with zero valid candidates.
	 * This is an arc consistency check to prevent greedy choices that cause future contradictions.
	 * @param StateIndex The state considering the candidate
	 * @param CandidateModule The module being considered
	 * @return True if selection is safe (neighbors would have at least one candidate), false otherwise
	 */
	bool CheckArcConsistency(int32 StateIndex, int32 CandidateModule) const;

	// SelectWeightedRandom inherited from base class
};

/**
 * Factory for entropy-based WFC solver.
 */
UCLASS(DisplayName = "Entropy Solver", meta=(ToolTip = "Entropy-based WFC solver. Collapses states with fewest candidates first.", PCGExNodeLibraryDoc="valency/valency-staging/entropy-solver"))
class PCGEXELEMENTSVALENCY_API UPCGExValencyEntropySolver : public UPCGExValencySolverInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExValencySolverOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(ValencyEntropySolver)
		NewOperation->MinimumSpawnWeightBoost = MinimumSpawnWeightBoost;
		return NewOperation;
	}

	/** Weight boost multiplier for modules that need more spawns to meet minimum */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1.0"))
	float MinimumSpawnWeightBoost = 2.0f;
};
