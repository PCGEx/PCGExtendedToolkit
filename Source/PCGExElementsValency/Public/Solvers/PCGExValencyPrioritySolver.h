// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExValencySolverOperation.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGExValencyPrioritySolver.generated.h"

namespace PCGExData
{
	class FFacade;
	class FFacadePreloader;
	template <typename T>
	class TBuffer;
}

/**
 * Allocations for priority solver - contains pre-sorted node indices by priority.
 */
struct PCGEXELEMENTSVALENCY_API FPrioritySolverAllocations : PCGExValency::FSolverAllocations
{
	/** Node indices sorted by priority (highest first) */
	TArray<int32> SortedNodeIndices;

	/** Priority values per node index (for logging/debugging) */
	TArray<float> NodePriorities;
};

/**
 * Per-state data for priority solver (internal).
 */
struct FPriorityStateData
{
	/** Valid candidate module indices (shrinks during propagation) */
	TArray<int32> Candidates;

	/** Filler module candidates - used as last resort when all normal candidates are eliminated */
	TArray<int32> FillerCandidates;

	/** Priority value for this state */
	float Priority = 0.0f;

	/** Reset state */
	void Reset()
	{
		Candidates.Empty();
		FillerCandidates.Empty();
		Priority = 0.0f;
	}
};

/**
 * Priority-based WFC solver.
 * Collapses states in order of highest priority (from attribute).
 * Within equal priorities, uses entropy (fewest candidates) as tiebreaker.
 */
class PCGEXELEMENTSVALENCY_API FPCGExValencyPrioritySolver : public FPCGExValencySolverOperation
{
public:
	FPCGExValencyPrioritySolver() = default;
	virtual ~FPCGExValencyPrioritySolver() override = default;

	virtual void Initialize(
		const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
		TArray<PCGExValency::FValencyState>& InValencyStates,
		const PCGExValency::FOrbitalCache* InOrbitalCache,
		int32 InSeed,
		const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations = nullptr) override;

	virtual PCGExValency::FSolveResult Solve() override;

protected:
	/** Priority allocations (cast from base Allocations) */
	TSharedPtr<FPrioritySolverAllocations> PriorityAllocations;

	/** Priority-specific state per valency state (parallel to ValencyStates) */
	TArray<FPriorityStateData> StateData;

	/** Current position in sorted node list */
	int32 CurrentSortedIndex = 0;

	/**
	 * Initialize candidates for all states based on orbital mask matching.
	 */
	void InitializeAllCandidates();

	/**
	 * Get the next unresolved state by priority order.
	 * @return State index, or -1 if all resolved
	 */
	int32 GetNextByPriority();

	/**
	 * Collapse a state by selecting a module from its candidates.
	 * @param StateIndex The state to collapse
	 * @return True if successful, false if no valid candidates (contradiction)
	 */
	bool CollapseState(int32 StateIndex);

	/**
	 * Propagate constraints from a resolved state to its neighbors.
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
	 * @param StateIndex The state considering the candidate
	 * @param CandidateModule The module being considered
	 * @return True if selection is safe, false otherwise
	 */
	bool CheckArcConsistency(int32 StateIndex, int32 CandidateModule) const;
};

/**
 * Factory for priority-based WFC solver.
 */
UCLASS(DisplayName = "Priority Solver", meta=(ToolTip = "Priority-based WFC solver. Collapses states in order of an attribute value (highest first).", PCGExNodeLibraryDoc="valency/valency-staging/priority-solver"))
class PCGEXELEMENTSVALENCY_API UPCGExValencyPrioritySolver : public UPCGExValencySolverInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExValencySolverOperation> CreateOperation() const override;

	virtual void RegisterPrimaryBuffersDependencies(
		FPCGExContext* InContext,
		PCGExData::FFacadePreloader& FacadePreloader) const override;

	virtual TSharedPtr<PCGExValency::FSolverAllocations> CreateAllocations(
		const TSharedRef<PCGExData::FFacade>& VtxFacade) const override;

	/** The attribute to read priority from. Higher values are solved first. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector PriorityAttribute;

	/** Weight boost multiplier for modules that need more spawns to meet minimum */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1.0"))
	float MinimumSpawnWeightBoost = 2.0f;

	/** If true, treat lower values as higher priority (invert priority order) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvertPriority = false;
};
