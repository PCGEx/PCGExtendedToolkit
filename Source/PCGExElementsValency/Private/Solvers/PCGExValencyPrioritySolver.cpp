// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Solvers/PCGExValencyPrioritySolver.h"
#include "Core/PCGExValencyLog.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"

TSharedPtr<FPCGExValencySolverOperation> UPCGExValencyPrioritySolver::CreateOperation() const
{
	PCGEX_FACTORY_NEW_OPERATION(ValencyPrioritySolver)
	NewOperation->MinimumSpawnWeightBoost = MinimumSpawnWeightBoost;
	return NewOperation;
}

void UPCGExValencyPrioritySolver::RegisterPrimaryBuffersDependencies(
	FPCGExContext* InContext,
	PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterPrimaryBuffersDependencies(InContext, FacadePreloader);

	// Register the priority attribute for preloading
	FacadePreloader.TryRegister(InContext, PriorityAttribute);
}

TSharedPtr<PCGExValency::FSolverAllocations> UPCGExValencyPrioritySolver::CreateAllocations(
	const TSharedRef<PCGExData::FFacade>& VtxFacade) const
{
	TSharedPtr<FPrioritySolverAllocations> Allocs = MakeShared<FPrioritySolverAllocations>();

	// Read priority values from attribute
	TSharedPtr<PCGExData::TBuffer<float>> PriorityReader = VtxFacade->GetBroadcaster<float>(PriorityAttribute);
	if (!PriorityReader)
	{
		// Fallback: if attribute not found, use uniform priority (index order)
		const int32 NumPoints = VtxFacade->GetNum();
		Allocs->SortedNodeIndices.SetNum(NumPoints);
		Allocs->NodePriorities.SetNum(NumPoints);
		for (int32 i = 0; i < NumPoints; ++i)
		{
			Allocs->SortedNodeIndices[i] = i;
			Allocs->NodePriorities[i] = 0.0f;
		}
		return Allocs;
	}

	const int32 NumPoints = VtxFacade->GetNum();
	Allocs->NodePriorities.SetNum(NumPoints);
	Allocs->SortedNodeIndices.SetNum(NumPoints);

	// Copy priority values and initialize indices
	for (int32 i = 0; i < NumPoints; ++i)
	{
		Allocs->NodePriorities[i] = PriorityReader->Read(i);
		Allocs->SortedNodeIndices[i] = i;
	}

	// Sort indices by priority (highest first, or lowest first if inverted)
	if (bInvertPriority)
	{
		Allocs->SortedNodeIndices.Sort([&Allocs](int32 A, int32 B)
		{
			return Allocs->NodePriorities[A] < Allocs->NodePriorities[B];
		});
	}
	else
	{
		Allocs->SortedNodeIndices.Sort([&Allocs](int32 A, int32 B)
		{
			return Allocs->NodePriorities[A] > Allocs->NodePriorities[B];
		});
	}

	return Allocs;
}

void FPCGExValencyPrioritySolver::Initialize(
	const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
	TArray<PCGExValency::FValencyState>& InValencyStates,
	const PCGExValency::FOrbitalCache* InOrbitalCache,
	int32 InSeed,
	const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations)
{
	FPCGExValencySolverOperation::Initialize(InCompiledBondingRules, InValencyStates, InOrbitalCache, InSeed, InAllocations);

	// Cast allocations to priority-specific type
	PriorityAllocations = StaticCastSharedPtr<FPrioritySolverAllocations>(Allocations);

	// Initialize state data
	const int32 NumStates = ValencyStates->Num();
	StateData.SetNum(NumStates);

	for (int32 i = 0; i < NumStates; ++i)
	{
		StateData[i].Reset();
		if (PriorityAllocations && PriorityAllocations->NodePriorities.IsValidIndex(i))
		{
			StateData[i].Priority = PriorityAllocations->NodePriorities[i];
		}
	}

	CurrentSortedIndex = 0;

	VALENCY_LOG_SECTION(Solver, "PRIORITY SOLVER INITIALIZED");
	PCGEX_VALENCY_INFO(Solver, "States: %d, Modules: %d", NumStates, CompiledBondingRules ? CompiledBondingRules->ModuleCount : 0);
}

PCGExValency::FSolveResult FPCGExValencyPrioritySolver::Solve()
{
	VALENCY_LOG_SECTION(Solver, "PRIORITY SOLVER STARTING");

	PCGExValency::FSolveResult Result;

	if (!CompiledBondingRules || !ValencyStates || !OrbitalCache)
	{
		PCGEX_VALENCY_ERROR(Solver, "Missing required data for solving");
		return Result;
	}

	// Initialize candidates for all states
	InitializeAllCandidates();

	// Main solving loop - normal candidates only, process in priority order
	int32 Iterations = 0;
	const int32 MaxIterations = ValencyStates->Num() * 2; // Safety limit

	while (Iterations < MaxIterations)
	{
		Iterations++;

		// Get next state by priority
		const int32 StateIndex = GetNextByPriority();
		if (StateIndex < 0)
		{
			break;
		}

		PCGEX_VALENCY_VERBOSE(Solver, "Iteration %d: Collapsing state %d (priority=%.2f, candidates=%d)",
			Iterations, StateIndex, StateData[StateIndex].Priority, StateData[StateIndex].Candidates.Num());

		// Collapse this state
		if (!CollapseState(StateIndex))
		{
			// Don't mark UNSOLVABLE yet if fillers are available - filler sweep will handle it
			if (FillerCandidatesPerState[StateIndex].Num() == 0)
			{
				(*ValencyStates)[StateIndex].ResolvedModule = PCGExValency::SlotState::UNSOLVABLE;
				PCGEX_VALENCY_WARNING(Solver, "State %d marked UNSOLVABLE (no valid candidates, no fillers)", StateIndex);
			}
			else
			{
				PCGEX_VALENCY_VERBOSE(Solver, "State %d deferred to filler sweep (%d fillers available)", StateIndex, FillerCandidatesPerState[StateIndex].Num());
			}
		}

		// Propagate constraints to neighbors
		PropagateConstraints(StateIndex);
	}

	// Filler sweep - assign filler modules to remaining unresolved states
	// Runs AFTER the main loop to avoid fillers interfering with normal constraint propagation
	SweepFillers();

	// Count results
	for (const PCGExValency::FValencyState& State : *ValencyStates)
	{
		if (State.ResolvedModule >= 0)
		{
			Result.ResolvedCount++;
		}
		else if (State.IsUnsolvable())
		{
			Result.UnsolvableCount++;
		}
		else if (State.IsBoundary())
		{
			Result.BoundaryCount++;
		}
	}

	Result.MinimumsSatisfied = DistributionTracker.AreMinimumsSatisfied();
	Result.bSuccess = Result.UnsolvableCount == 0;

	VALENCY_LOG_SECTION(Solver, "PRIORITY SOLVER COMPLETE");
	PCGEX_VALENCY_INFO(Solver, "Iterations: %d, Resolved: %d, Unsolvable: %d, Boundary: %d",
		Iterations, Result.ResolvedCount, Result.UnsolvableCount, Result.BoundaryCount);

	return Result;
}

void FPCGExValencyPrioritySolver::InitializeAllCandidates()
{
	VALENCY_LOG_SUBSECTION(Solver, "Initializing Candidates");

	for (int32 StateIndex = 0; StateIndex < ValencyStates->Num(); ++StateIndex)
	{
		PCGExValency::FValencyState& State = (*ValencyStates)[StateIndex];
		FPriorityStateData& Data = StateData[StateIndex];

		// Skip already resolved states (boundaries)
		if (State.IsResolved())
		{
			Data.Candidates.Empty();
			continue;
		}

		// Find Normal modules that fit this state's orbital configuration
		Data.Candidates.Empty();
		FillerCandidatesPerState[StateIndex].Empty();

		for (int32 ModuleIndex = 0; ModuleIndex < CompiledBondingRules->ModuleCount; ++ModuleIndex)
		{
			if (CompiledBondingRules->IsModuleExcluded(ModuleIndex)) { continue; }

			if (DoesModuleFitNode(ModuleIndex, StateIndex))
			{
				if (CompiledBondingRules->IsModuleFiller(ModuleIndex))
				{
					FillerCandidatesPerState[StateIndex].Add(ModuleIndex);
				}
				else
				{
					Data.Candidates.Add(ModuleIndex);
				}
			}
		}

		PCGEX_VALENCY_VERBOSE(Solver, "State[%d]: %d candidates, %d fillers, priority=%.2f",
			StateIndex, Data.Candidates.Num(), FillerCandidatesPerState[StateIndex].Num(), Data.Priority);
	}
}

int32 FPCGExValencyPrioritySolver::GetNextByPriority()
{
	if (!PriorityAllocations)
	{
		// Fallback: find first unresolved state with normal candidates
		for (int32 i = 0; i < ValencyStates->Num(); ++i)
		{
			if (!(*ValencyStates)[i].IsResolved() && StateData[i].Candidates.Num() > 0)
			{
				return i;
			}
		}
		return -1;
	}

	// Iterate through sorted indices to find next unresolved state
	while (CurrentSortedIndex < PriorityAllocations->SortedNodeIndices.Num())
	{
		const int32 StateIndex = PriorityAllocations->SortedNodeIndices[CurrentSortedIndex];
		CurrentSortedIndex++;

		// Validate state index is in bounds
		if (!ValencyStates->IsValidIndex(StateIndex) || !StateData.IsValidIndex(StateIndex))
		{
			continue;
		}

		// Skip already resolved states
		if ((*ValencyStates)[StateIndex].IsResolved())
		{
			continue;
		}

		// Skip states with no normal candidates - filler-only states are handled in the post-solve sweep
		if (StateData[StateIndex].Candidates.Num() == 0)
		{
			continue;
		}

		return StateIndex;
	}

	return -1;
}

bool FPCGExValencyPrioritySolver::CollapseState(int32 StateIndex)
{
	FPriorityStateData& Data = StateData[StateIndex];
	PCGExValency::FValencyState& State = (*ValencyStates)[StateIndex];

	if (Data.Candidates.Num() == 0)
	{
		return false;
	}

	// Check for forced selection (minimum spawn constraints)
	PCGExValency::FSlotBudget SlotBudget;
	SlotBudget.Initialize(
		CompiledBondingRules,
		*ValencyStates,
		OrbitalCache,
		[this](int32 ModuleIndex, int32 NodeIndex) { return DoesModuleFitNode(ModuleIndex, NodeIndex); });

	const int32 ForcedModule = SlotBudget.GetForcedSelection(Data.Candidates, DistributionTracker, CompiledBondingRules);

	int32 SelectedModule;
	if (ForcedModule >= 0)
	{
		SelectedModule = ForcedModule;
		PCGEX_VALENCY_VERBOSE(Solver, "  FORCED selection: Module[%d] (minimum spawn constraint)", SelectedModule);
	}
	else
	{
		// Filter candidates by arc consistency
		TArray<int32> SafeCandidates;
		for (int32 Candidate : Data.Candidates)
		{
			if (DistributionTracker.CanSpawn(Candidate) && CheckArcConsistency(StateIndex, Candidate))
			{
				SafeCandidates.Add(Candidate);
			}
		}

		if (SafeCandidates.Num() == 0)
		{
			// Fall back to all candidates if arc consistency is too strict
			SafeCandidates = Data.Candidates.FilterByPredicate([this](int32 Candidate)
			{
				return DistributionTracker.CanSpawn(Candidate);
			});
		}

		if (SafeCandidates.Num() == 0)
		{
			return false;
		}

		SelectedModule = SelectWeightedRandom(SafeCandidates);
	}

	// Resolve the state
	State.ResolvedModule = SelectedModule;
	DistributionTracker.RecordSpawn(SelectedModule, CompiledBondingRules);
	Data.Candidates.Empty();

	PCGEX_VALENCY_VERBOSE(Solver, "  State[%d] collapsed to Module[%d]", StateIndex, SelectedModule);

	return true;
}

void FPCGExValencyPrioritySolver::PropagateConstraints(int32 ResolvedStateIndex)
{
	if (!OrbitalCache)
	{
		return;
	}

	// Filler modules have no neighbor data - skip propagation to avoid corrupting neighbor candidates
	const int32 ResolvedModule = (*ValencyStates)[ResolvedStateIndex].ResolvedModule;
	if (ResolvedModule >= 0 && CompiledBondingRules->IsModuleFiller(ResolvedModule))
	{
		return;
	}

	const int32 MaxOrbitals = OrbitalCache->GetMaxOrbitals();

	for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals; ++OrbitalIndex)
	{
		const int32 NeighborIndex = OrbitalCache->GetNeighborAtOrbital(ResolvedStateIndex, OrbitalIndex);
		if (NeighborIndex < 0)
		{
			continue;
		}

		// Skip already resolved neighbors
		if ((*ValencyStates)[NeighborIndex].IsResolved())
		{
			continue;
		}

		// Filter neighbor's candidates based on new constraint
		if (!FilterCandidates(NeighborIndex))
		{
			PCGEX_VALENCY_WARNING(Solver, "Neighbor[%d] has no valid candidates after propagation from State[%d]",
				NeighborIndex, ResolvedStateIndex);
		}
	}
}

bool FPCGExValencyPrioritySolver::FilterCandidates(int32 StateIndex)
{
	FPriorityStateData& Data = StateData[StateIndex];
	const int32 MaxOrbitals = OrbitalCache ? OrbitalCache->GetMaxOrbitals() : 0;

	TArray<int32> ValidCandidates;

	for (int32 CandidateModule : Data.Candidates)
	{
		bool bValid = true;

		// Check compatibility with all resolved neighbors
		for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals && bValid; ++OrbitalIndex)
		{
			const int32 NeighborIndex = OrbitalCache->GetNeighborAtOrbital(StateIndex, OrbitalIndex);
			if (NeighborIndex < 0)
			{
				continue;
			}

			const PCGExValency::FValencyState& NeighborState = (*ValencyStates)[NeighborIndex];
			if (!NeighborState.IsResolved() || NeighborState.ResolvedModule < 0)
			{
				continue;
			}

			// Check if candidate is compatible with resolved neighbor
			if (!IsModuleCompatibleWithNeighbor(CandidateModule, OrbitalIndex, NeighborState.ResolvedModule))
			{
				bValid = false;
			}
		}

		if (bValid)
		{
			ValidCandidates.Add(CandidateModule);
		}
	}

	Data.Candidates = MoveTemp(ValidCandidates);
	return Data.Candidates.Num() > 0;
}

bool FPCGExValencyPrioritySolver::CheckArcConsistency(int32 StateIndex, int32 CandidateModule) const
{
	if (!OrbitalCache)
	{
		return true;
	}

	const int32 MaxOrbitals = OrbitalCache->GetMaxOrbitals();

	for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals; ++OrbitalIndex)
	{
		const int32 NeighborIndex = OrbitalCache->GetNeighborAtOrbital(StateIndex, OrbitalIndex);
		if (NeighborIndex < 0)
		{
			continue;
		}

		// Skip already resolved neighbors
		if ((*ValencyStates)[NeighborIndex].IsResolved())
		{
			continue;
		}

		const FPriorityStateData& NeighborData = StateData[NeighborIndex];

		// Find which orbital of the neighbor points back to us
		int32 ReverseOrbital = -1;
		for (int32 NeighborOrbital = 0; NeighborOrbital < MaxOrbitals; ++NeighborOrbital)
		{
			if (OrbitalCache->GetNeighborAtOrbital(NeighborIndex, NeighborOrbital) == StateIndex)
			{
				ReverseOrbital = NeighborOrbital;
				break;
			}
		}

		if (ReverseOrbital < 0)
		{
			continue; // No reverse connection (unusual but possible)
		}

		// Check if neighbor would have at least one valid candidate after this selection
		bool bNeighborHasValidCandidate = false;
		for (int32 NeighborCandidate : NeighborData.Candidates)
		{
			if (IsModuleCompatibleWithNeighbor(NeighborCandidate, ReverseOrbital, CandidateModule))
			{
				bNeighborHasValidCandidate = true;
				break;
			}
		}

		if (!bNeighborHasValidCandidate)
		{
			return false;
		}
	}

	return true;
}
