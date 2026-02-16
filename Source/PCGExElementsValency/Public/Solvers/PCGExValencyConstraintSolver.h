// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExValencySolverOperation.h"

#include "PCGExValencyConstraintSolver.generated.h"

/**
 * Constraint-aware WFC solver.
 * Like entropy solver but with slot budget tracking for guaranteed min spawn satisfaction.
 *
 * Key differences from Entropy Solver:
 * - Tracks available slots per module via FSlotBudget
 * - Forces selection when urgency >= 1.0 (no slots to spare)
 * - Detects unsolvability early (urgency > 1.0 = impossible)
 * - Uses urgency-based boosting instead of flat MinimumSpawnWeightBoost
 */
class PCGEXELEMENTSVALENCY_API FPCGExValencyConstraintSolver : public FPCGExValencySolverOperation
{
public:
	FPCGExValencyConstraintSolver() = default;
	virtual ~FPCGExValencyConstraintSolver() override = default;

	/** Multiplier for urgency-based weight boost (applied as: weight *= 1 + urgency * UrgencyBoostMultiplier) */
	float UrgencyBoostMultiplier = 10.0f;

	virtual void Initialize(
		const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
		TArray<PCGExValency::FValencyState>& InValencyStates,
		const PCGExValency::FOrbitalCache* InOrbitalCache,
		int32 InSeed,
		const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations = nullptr) override;

	virtual PCGExValency::FSolveResult Solve() override;

protected:
	/** Per-state candidate tracking (reuses FWFCStateData pattern) */
	struct FConstraintStateData
	{
		/** Valid candidate module indices */
		TArray<int32> Candidates;

		/** Cached entropy value for priority queue ordering */
		float Entropy = TNumericLimits<float>::Max();

		/** Ratio of resolved neighbors (higher = more constrained, used as tiebreaker) */
		float NeighborResolutionRatio = 0.0f;

		void Reset()
		{
			Candidates.Empty();
			Entropy = TNumericLimits<float>::Max();
			NeighborResolutionRatio = 0.0f;
		}
	};

	/** State data parallel to ValencyStates */
	TArray<FConstraintStateData> StateData;

	/** Priority queue of unresolved state indices */
	TArray<int32> EntropyQueue;

	/** Slot budget tracker for constraint-aware selection */
	PCGExValency::FSlotBudget SlotBudget;

	/** Initialize candidates for all states based on orbital mask matching */
	void InitializeAllCandidates();

	/** Calculate entropy for a state */
	void UpdateEntropy(int32 StateIndex);

	/** Rebuild the entropy queue from all unresolved states */
	void RebuildEntropyQueue();

	/** Pop the lowest entropy state from the queue */
	int32 PopLowestEntropy();

	/**
	 * Collapse a state by selecting a module from its candidates.
	 * Uses constraint-aware selection with forced picks when urgency >= 1.0.
	 * @return True if successful, false if contradiction
	 */
	bool CollapseState(int32 StateIndex);

	/** Propagate constraints from a resolved state to its neighbors */
	void PropagateConstraints(int32 ResolvedStateIndex);

	/**
	 * Filter candidates for a state based on resolved neighbor constraints.
	 * @return True if candidates remain, false if empty (contradiction)
	 */
	bool FilterCandidates(int32 StateIndex);

	/**
	 * Arc consistency check - would selecting this candidate leave any neighbor with zero candidates?
	 */
	bool CheckArcConsistency(int32 StateIndex, int32 CandidateModule) const;

	/**
	 * Select a module using constraint-aware logic:
	 * 1. Check for forced selection (urgency >= 1.0)
	 * 2. Otherwise use weighted random with urgency-based boosting
	 */
	int32 SelectWithConstraints(const TArray<int32>& Candidates);
};

/**
 * Factory for constraint-aware WFC solver.
 */
UCLASS(DisplayName = "Constraint Solver", meta=(ToolTip = "Constraint-aware WFC solver with guaranteed min spawn satisfaction when possible.", PCGExNodeLibraryDoc="valency/valency-staging/constraint-solver"))
class PCGEXELEMENTSVALENCY_API UPCGExValencyConstraintSolverFactory : public UPCGExValencySolverInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExValencySolverOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(ValencyConstraintSolver)
		NewOperation->UrgencyBoostMultiplier = UrgencyBoostMultiplier;
		NewOperation->MinimumSpawnWeightBoost = MinimumSpawnWeightBoost;
		return NewOperation;
	}

	/** Multiplier for urgency-based weight boost. Higher = more aggressive prioritization of constrained modules. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1.0"))
	float UrgencyBoostMultiplier = 10.0f;

	/** Fallback weight boost for modules needing minimum spawns (used when urgency < 1.0) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1.0"))
	float MinimumSpawnWeightBoost = 2.0f;
};
