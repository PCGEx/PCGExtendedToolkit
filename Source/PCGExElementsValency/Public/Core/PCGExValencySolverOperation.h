// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"
#include "PCGExValencyCommon.h"
#include "PCGExValencyBondingRules.h"
#include "PCGExValencyOrbitalCache.h"

#include "PCGExValencySolverOperation.generated.h"

namespace PCGExData
{
	class FFacade;
	class FFacadePreloader;
}

namespace PCGExValency
{
	// FValencyState is now in PCGExValencyCommon.h

	/**
	 * Base class for solver-specific shared data/allocations.
	 * Solvers that need access to point attributes (e.g., priority) should derive from this
	 * and override CreateAllocations() in their factory to populate it.
	 */
	struct PCGEXELEMENTSVALENCY_API FSolverAllocations : TSharedFromThis<FSolverAllocations>
	{
		virtual ~FSolverAllocations() = default;
	};

	/**
	 * Distribution constraint tracker for min/max spawn counts.
	 */
	struct PCGEXELEMENTSVALENCY_API FDistributionTracker
	{
		/** Current spawn count per module index */
		TArray<int32> SpawnCounts;

		/** Modules that still need more spawns to meet minimum */
		TSet<int32> ModulesNeedingMinimum;

		/** Modules that have reached their maximum */
		TSet<int32> ModulesAtMaximum;

		/** Initialize from compiled bonding rules */
		void Initialize(const FPCGExValencyBondingRulesCompiled* CompiledBondingRules);

		/** Record a module spawn, update constraints. Returns false if would exceed max. */
		bool RecordSpawn(int32 ModuleIndex, const FPCGExValencyBondingRulesCompiled* CompiledBondingRules);

		/** Check if a module can still be spawned */
		bool CanSpawn(int32 ModuleIndex) const;

		/** Check if minimum constraints are satisfied */
		bool AreMinimumsSatisfied() const { return ModulesNeedingMinimum.Num() == 0; }

		/** Get modules that must be spawned to meet minimums */
		const TSet<int32>& GetModulesNeedingMinimum() const { return ModulesNeedingMinimum; }
	};

	/**
	 * Result of a solve operation.
	 */
	struct PCGEXELEMENTSVALENCY_API FSolveResult
	{
		/** Number of successfully resolved nodes */
		int32 ResolvedCount = 0;

		/** Number of nodes marked as unsolvable (contradictions) */
		int32 UnsolvableCount = 0;

		/** Number of nodes marked as boundary/null */
		int32 BoundaryCount = 0;

		/** True if all minimum spawn constraints were satisfied */
		bool MinimumsSatisfied = true;

		/** True if solving completed without critical errors */
		bool bSuccess = false;
	};

	/** Function type for checking if a module fits a node using orbital cache */
	using FModuleFitChecker = TFunctionRef<bool(int32 ModuleIndex, int32 NodeIndex)>;

	/**
	 * Tracks available slots per module for constraint-aware selection.
	 * Enables forced selection when min spawns are at risk of not being met.
	 */
	struct PCGEXELEMENTSVALENCY_API FSlotBudget
	{
		/** Per-module: count of unresolved states where module could still fit */
		TArray<int32> AvailableSlots;

		/** Per-state: which modules fit this state (for fast slot decrement on collapse) */
		TArray<TArray<int32>> StateToFittingModules;

		/** Initialize slot tracking from compiled rules and orbital cache */
		void Initialize(
			const FPCGExValencyBondingRulesCompiled* Rules,
			const TArray<FValencyState>& States,
			const FOrbitalCache* Cache,
			FModuleFitChecker FitChecker);

		/** Call when a state is collapsed - decrements AvailableSlots for all fitting modules */
		void OnStateCollapsed(int32 StateIndex);

		/**
		 * Calculate urgency for a module: how critical is it to select this module now?
		 * @return 0.0 = no urgency (min satisfied), 0.0-1.0 = some urgency, >=1.0 = must select now, >1.0 = impossible
		 */
		float GetUrgency(
			int32 ModuleIndex,
			const FDistributionTracker& Tracker,
			const FPCGExValencyBondingRulesCompiled* Rules) const;

		/**
		 * Check if any candidate MUST be selected (urgency >= 1.0) to meet minimums.
		 * @return Module index if forced selection needed, -1 otherwise
		 */
		int32 GetForcedSelection(
			const TArray<int32>& Candidates,
			const FDistributionTracker& Tracker,
			const FPCGExValencyBondingRulesCompiled* Rules) const;

		/**
		 * Check if any module has become impossible to satisfy (urgency > 1.0).
		 * @return true if constraints are still satisfiable
		 */
		bool AreConstraintsSatisfiable(
			const FDistributionTracker& Tracker,
			const FPCGExValencyBondingRulesCompiled* Rules) const;
	};
}

/**
 * Base class for Valency solver operations.
 * Derive from this to create custom solving algorithms (WFC, Chemistry, etc.).
 *
 * Solvers receive ValencyStates with input data (orbital masks, neighbor mapping)
 * and must write ResolvedModule to each state.
 */
class PCGEXELEMENTSVALENCY_API FPCGExValencySolverOperation : public FPCGExOperation
{
public:
	FPCGExValencySolverOperation() = default;
	virtual ~FPCGExValencySolverOperation() override = default;

	/**
	 * Initialize the solver with bonding rules, states, and orbital cache.
	 * Override to set up solver-specific state.
	 * @param InCompiledBondingRules The compiled bonding rules with module/orbital definitions
	 * @param InValencyStates Array of states (one per cluster node) - solver writes ResolvedModule
	 * @param InOrbitalCache Orbital cache providing orbital masks and neighbor mappings
	 * @param InSeed Random seed for deterministic solving
	 * @param InAllocations Optional solver-specific allocations (e.g., priority data)
	 */
	virtual void Initialize(
		const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
		TArray<PCGExValency::FValencyState>& InValencyStates,
		const PCGExValency::FOrbitalCache* InOrbitalCache,
		int32 InSeed,
		const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations = nullptr);

	/**
	 * Run the full solve algorithm.
	 * Must set ResolvedModule on each state.
	 * @return Result containing success state and statistics
	 */
	virtual PCGExValency::FSolveResult Solve() PCGEX_NOT_IMPLEMENTED_RET(Solve, PCGExValency::FSolveResult());

	/** Get the distribution tracker for inspection */
	const PCGExValency::FDistributionTracker& GetDistributionTracker() const { return DistributionTracker; }

	/** Weight boost multiplier for modules that need more spawns to meet minimum */
	float MinimumSpawnWeightBoost = 2.0f;

	/**
	 * Check if a module's orbital mask matches a node's available orbitals.
	 * Public because FSlotBudget needs to call this during initialization.
	 * @param ModuleIndex Module to check
	 * @param NodeIndex Node index to check against (uses OrbitalCache)
	 * @return True if module can fit at this node based on orbital geometry
	 */
	bool DoesModuleFitNode(int32 ModuleIndex, int32 NodeIndex) const;

protected:
	/** The compiled bonding rules */
	const FPCGExValencyBondingRulesCompiled* CompiledBondingRules = nullptr;

	/** Valency states (owned externally by staging node) */
	TArray<PCGExValency::FValencyState>* ValencyStates = nullptr;

	/** Orbital cache providing orbital masks and neighbor mappings */
	const PCGExValency::FOrbitalCache* OrbitalCache = nullptr;

	/** Solver-specific allocations (optional, provided by factory) */
	TSharedPtr<PCGExValency::FSolverAllocations> Allocations;

	/** Distribution constraint tracker (shared utility) */
	PCGExValency::FDistributionTracker DistributionTracker;

	/** Per-state filler candidates - populated by InitializeAllCandidates, consumed by SweepFillers */
	TArray<TArray<int32>> FillerCandidatesPerState;

	/** Random stream for deterministic selection */
	FRandomStream RandomStream;

	/**
	 * Assign filler modules to all remaining unresolved states.
	 * Call at the end of Solve() after the main loop completes.
	 * Fillers bypass constraint propagation entirely to avoid domino effects.
	 * @return Number of states assigned a filler module
	 */
	int32 SweepFillers();

	/**
	 * Check if a module is compatible with a neighbor at a specific orbital.
	 * Utility for solvers that need adjacency checking.
	 */
	bool IsModuleCompatibleWithNeighbor(int32 ModuleIndex, int32 OrbitalIndex, int32 NeighborModuleIndex) const;

	/** Get neighbor node index at orbital for a node (-1 if no neighbor) */
	FORCEINLINE int32 GetNeighborAtOrbital(int32 NodeIndex, int32 OrbitalIndex) const
	{
		return OrbitalCache ? OrbitalCache->GetNeighborAtOrbital(NodeIndex, OrbitalIndex) : -1;
	}

	/** Get orbital mask for a node */
	FORCEINLINE int64 GetOrbitalMask(int32 NodeIndex) const
	{
		return OrbitalCache ? OrbitalCache->GetOrbitalMask(NodeIndex) : 0;
	}

	/** Check if a node has any orbitals (non-zero mask) */
	FORCEINLINE bool HasOrbitals(int32 NodeIndex) const
	{
		return OrbitalCache ? OrbitalCache->HasOrbitals(NodeIndex) : false;
	}

	/** Get max orbital count from cache */
	FORCEINLINE int32 GetMaxOrbitals() const
	{
		return OrbitalCache ? OrbitalCache->GetMaxOrbitals() : 0;
	}

	/**
	 * Select a module from candidates using weighted random.
	 * Considers distribution constraints (boosts modules needing minimum spawns).
	 * @param Candidates Array of valid module indices to choose from
	 * @return Selected module index, or -1 if candidates is empty
	 */
	int32 SelectWeightedRandom(const TArray<int32>& Candidates);
};

/**
 * Base factory for creating Valency solver operations.
 * Subclass this and override CreateOperation() to provide custom solvers.
 */
UCLASS(Abstract)
class PCGEXELEMENTSVALENCY_API UPCGExValencySolverInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	virtual TSharedPtr<FPCGExValencySolverOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation, nullptr);

	/**
	 * Register buffer dependencies for this solver.
	 * Override to declare attributes that need preloading (e.g., priority attribute).
	 * Called during batch's RegisterBuffersDependencies phase.
	 */
	virtual void RegisterPrimaryBuffersDependencies(
		FPCGExContext* InContext,
		PCGExData::FFacadePreloader& FacadePreloader) const override;

	/**
	 * Create solver-specific allocations from the vertex facade.
	 * Override to read attributes and build data structures needed by the solver.
	 * Called after buffers are preloaded, during OnProcessingPreparationComplete.
	 * @param VtxFacade The vertex data facade with preloaded buffers
	 * @return Allocations object, or nullptr if solver doesn't need allocations
	 */
	virtual TSharedPtr<PCGExValency::FSolverAllocations> CreateAllocations(
		const TSharedRef<PCGExData::FFacade>& VtxFacade) const;
};
