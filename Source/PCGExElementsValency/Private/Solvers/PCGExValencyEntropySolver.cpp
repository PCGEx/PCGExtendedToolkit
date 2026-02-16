// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Solvers/PCGExValencyEntropySolver.h"
#include "Core/PCGExValencyLog.h"

#define LOCTEXT_NAMESPACE "PCGExValencyEntropySolver"
#define PCGEX_NAMESPACE ValencyEntropySolver

void FPCGExValencyEntropySolver::Initialize(
	const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
	TArray<PCGExValency::FValencyState>& InValencyStates,
	const PCGExValency::FOrbitalCache* InOrbitalCache,
	int32 InSeed,
	const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations)
{
	VALENCY_LOG_SECTION(Solver, "ENTROPY SOLVER INITIALIZE");
	PCGEX_VALENCY_INFO(Solver, "Seed: %d, States: %d, CompiledRules: %s, OrbitalCache: %s",
		InSeed, InValencyStates.Num(),
		InCompiledBondingRules ? TEXT("Valid") : TEXT("NULL"),
		InOrbitalCache ? TEXT("Valid") : TEXT("NULL"));

	if (InCompiledBondingRules)
	{
		PCGEX_VALENCY_INFO(Solver, "  CompiledRules: %d modules",
			InCompiledBondingRules->ModuleCount);
	}

	// Call base - marks boundary states
	FPCGExValencySolverOperation::Initialize(InCompiledBondingRules, InValencyStates, InOrbitalCache, InSeed, InAllocations);

	// Initialize WFC-specific state
	StateData.SetNum(ValencyStates->Num());
	for (FWFCStateData& Data : StateData)
	{
		Data.Reset();
	}

	// Initialize candidates for all states
	InitializeAllCandidates();

	// Calculate initial entropy for all states
	for (int32 i = 0; i < ValencyStates->Num(); ++i)
	{
		UpdateEntropy(i);
	}

	// Build initial entropy queue
	RebuildEntropyQueue();

	VALENCY_LOG_SECTION(Solver, "ENTROPY SOLVER INIT COMPLETE");
	PCGEX_VALENCY_INFO(Solver, "Queue size=%d", EntropyQueue.Num());
}

void FPCGExValencyEntropySolver::InitializeAllCandidates()
{
	if (!CompiledBondingRules || !OrbitalCache)
	{
		PCGEX_VALENCY_ERROR(Solver, "InitializeAllCandidates: CompiledBondingRules or OrbitalCache is NULL!");
		return;
	}

	VALENCY_LOG_SUBSECTION(Solver, "Initializing Candidates");
	PCGEX_VALENCY_INFO(Solver, "States: %d, Modules to check: %d", ValencyStates->Num(), CompiledBondingRules->ModuleCount);

	int32 TotalCandidates = 0;
	int32 UnsolvableCount = 0;

	for (int32 StateIndex = 0; StateIndex < ValencyStates->Num(); ++StateIndex)
	{
		const PCGExValency::FValencyState& State = (*ValencyStates)[StateIndex];
		FWFCStateData& Data = StateData[StateIndex];

		// Skip already resolved (boundary) states
		if (State.IsResolved())
		{
			PCGEX_VALENCY_VERBOSE(Solver, "  State[%d]: ALREADY RESOLVED (ResolvedModule=%d)", StateIndex, State.ResolvedModule);
			continue;
		}

		Data.Candidates.Empty();
		FillerCandidatesPerState[StateIndex].Empty();

		// Get node orbital mask for logging (using cache)
		const int64 NodeMask = GetOrbitalMask(StateIndex);

		// For each module, check if it fits this node
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

		TotalCandidates += Data.Candidates.Num();

		if (Data.Candidates.Num() == 0 && FillerCandidatesPerState[StateIndex].Num() == 0 && HasOrbitals(StateIndex))
		{
			PCGEX_VALENCY_WARNING(Solver, "  State[%d]: UNSOLVABLE - NodeMask=0x%llX, no modules fit!", StateIndex, NodeMask);
			(*ValencyStates)[StateIndex].ResolvedModule = PCGExValency::SlotState::UNSOLVABLE;
			UnsolvableCount++;
		}
		else
		{
			PCGEX_VALENCY_VERBOSE(Solver, "  State[%d]: NodeMask=0x%llX, %d candidates, %d fillers: [%s]",
				StateIndex, NodeMask, Data.Candidates.Num(), FillerCandidatesPerState[StateIndex].Num(),
				*FString::JoinBy(Data.Candidates, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));
		}
	}

	VALENCY_LOG_SUBSECTION(Solver, "Candidates Init Complete");
	PCGEX_VALENCY_INFO(Solver, "Total candidates=%d, Unsolvable=%d", TotalCandidates, UnsolvableCount);
}

void FPCGExValencyEntropySolver::UpdateEntropy(int32 StateIndex)
{
	if (!ValencyStates->IsValidIndex(StateIndex) || !OrbitalCache)
	{
		return;
	}

	const PCGExValency::FValencyState& State = (*ValencyStates)[StateIndex];
	FWFCStateData& Data = StateData[StateIndex];

	if (State.IsResolved())
	{
		Data.Entropy = TNumericLimits<float>::Max();
		return;
	}

	// Base entropy is candidate count
	Data.Entropy = static_cast<float>(Data.Candidates.Num());

	// Tiebreaker: ratio of resolved neighbors (more resolved = process sooner)
	int32 ResolvedNeighbors = 0;
	int32 TotalNeighbors = 0;

	const int32 MaxOrbitals = GetMaxOrbitals();
	for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals; ++OrbitalIndex)
	{
		const int32 NeighborIndex = GetNeighborAtOrbital(StateIndex, OrbitalIndex);
		if (NeighborIndex >= 0 && ValencyStates->IsValidIndex(NeighborIndex))
		{
			TotalNeighbors++;
			if ((*ValencyStates)[NeighborIndex].IsResolved())
			{
				ResolvedNeighbors++;
			}
		}
	}

	if (TotalNeighbors > 0)
	{
		Data.NeighborResolutionRatio = static_cast<float>(ResolvedNeighbors) / static_cast<float>(TotalNeighbors);
		// Subtract small amount so higher resolution ratio = lower entropy = processed sooner
		Data.Entropy -= Data.NeighborResolutionRatio * 0.5f;
	}
}

void FPCGExValencyEntropySolver::RebuildEntropyQueue()
{
	EntropyQueue.Empty();

	for (int32 i = 0; i < ValencyStates->Num(); ++i)
	{
		// Only queue states with normal candidates - filler-only states are handled in the post-solve sweep
		if (!(*ValencyStates)[i].IsResolved() && StateData[i].Candidates.Num() > 0)
		{
			EntropyQueue.Add(i);
		}
	}

	// Sort by entropy (ascending)
	EntropyQueue.Sort([this](int32 A, int32 B)
	{
		return StateData[A].Entropy < StateData[B].Entropy;
	});
}

int32 FPCGExValencyEntropySolver::PopLowestEntropy()
{
	if (EntropyQueue.Num() == 0)
	{
		return -1;
	}

	// Find lowest entropy in queue (queue should be sorted, but re-check due to updates)
	int32 BestQueueIndex = -1;
	float BestEntropy = TNumericLimits<float>::Max();

	for (int32 i = 0; i < EntropyQueue.Num(); ++i)
	{
		const int32 StateIndex = EntropyQueue[i];

		// Validate state index is in bounds
		if (!ValencyStates->IsValidIndex(StateIndex) || !StateData.IsValidIndex(StateIndex))
		{
			continue;
		}

		if (!(*ValencyStates)[StateIndex].IsResolved() && StateData[StateIndex].Entropy < BestEntropy)
		{
			BestEntropy = StateData[StateIndex].Entropy;
			BestQueueIndex = i;
		}
	}

	if (BestQueueIndex < 0)
	{
		return -1;
	}

	const int32 Result = EntropyQueue[BestQueueIndex];
	EntropyQueue.RemoveAt(BestQueueIndex);
	return Result;
}

PCGExValency::FSolveResult FPCGExValencyEntropySolver::Solve()
{
	PCGExValency::FSolveResult Result;

	VALENCY_LOG_SECTION(Solver, "ENTROPY SOLVER SOLVE START");

	if (!CompiledBondingRules || !ValencyStates)
	{
		PCGEX_VALENCY_ERROR(Solver, "Solve: Missing CompiledBondingRules or ValencyStates!");
		return Result;
	}

	// Count initial boundaries
	for (const PCGExValency::FValencyState& State : *ValencyStates)
	{
		if (State.IsBoundary())
		{
			Result.BoundaryCount++;
		}
	}

	PCGEX_VALENCY_INFO(Solver, "Initial boundaries: %d, Queue size: %d", Result.BoundaryCount, EntropyQueue.Num());

	// Main solve loop - normal candidates only
	int32 Iteration = 0;
	while (EntropyQueue.Num() > 0)
	{
		const int32 StateIndex = PopLowestEntropy();
		if (StateIndex < 0)
		{
			break;
		}

		PCGEX_VALENCY_VERBOSE(Solver, "--- Solve Iteration %d: Processing State[%d], Entropy=%.2f ---",
			Iteration, StateIndex, StateData[StateIndex].Entropy);

		if (!CollapseState(StateIndex))
		{
			PCGEX_VALENCY_VERBOSE(Solver, "  State[%d] CONTRADICTION during normal pass", StateIndex);
		}

		Iteration++;
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
	}

	Result.MinimumsSatisfied = DistributionTracker.AreMinimumsSatisfied();
	Result.bSuccess = (Result.UnsolvableCount == 0) && Result.MinimumsSatisfied;

	VALENCY_LOG_SECTION(Solver, "ENTROPY SOLVER SOLVE COMPLETE");
	PCGEX_VALENCY_INFO(Solver, "Iterations: %d, Resolved: %d, Unsolvable: %d, Boundaries: %d",
		Iteration, Result.ResolvedCount, Result.UnsolvableCount, Result.BoundaryCount);

	return Result;
}

bool FPCGExValencyEntropySolver::CollapseState(int32 StateIndex)
{
	if (!ValencyStates->IsValidIndex(StateIndex))
	{
		return false;
	}

	PCGExValency::FValencyState& State = (*ValencyStates)[StateIndex];
	FWFCStateData& Data = StateData[StateIndex];

	// Already resolved (shouldn't happen, but safety check)
	if (State.IsResolved())
	{
		PCGEX_VALENCY_VERBOSE(Solver, "  CollapseState[%d]: Already resolved with module %d", StateIndex, State.ResolvedModule);
		return true;
	}

	PCGEX_VALENCY_VERBOSE(Solver, "  CollapseState[%d]: Candidates before filter: %d", StateIndex, Data.Candidates.Num());

	// Filter candidates based on current neighbor states
	if (!FilterCandidates(StateIndex))
	{
		// Normal candidates exhausted - don't mark UNSOLVABLE yet, filler sweep will handle it
		if (FillerCandidatesPerState[StateIndex].Num() > 0)
		{
			PCGEX_VALENCY_VERBOSE(Solver, "  CollapseState[%d]: Normal candidates exhausted, %d fillers available for post-solve sweep", StateIndex, FillerCandidatesPerState[StateIndex].Num());
		}
		else
		{
			PCGEX_VALENCY_WARNING(Solver, "  CollapseState[%d]: NO CANDIDATES after filter, no fillers available", StateIndex);
			State.ResolvedModule = PCGExValency::SlotState::UNSOLVABLE;
		}
		return false;
	}

	PCGEX_VALENCY_VERBOSE(Solver, "  CollapseState[%d]: Candidates after filter: %d [%s]",
		StateIndex, Data.Candidates.Num(),
		*FString::JoinBy(Data.Candidates, TEXT(", "), [](int32 Idx) { return FString::FromInt(Idx); }));

	// Select module using weighted random
	const int32 SelectedModule = SelectWeightedRandom(Data.Candidates);

	if (SelectedModule < 0)
	{
		PCGEX_VALENCY_WARNING(Solver, "  CollapseState[%d]: SelectWeightedRandom returned -1!", StateIndex);
		State.ResolvedModule = PCGExValency::SlotState::UNSOLVABLE;
		return false;
	}

	// Record the selection
	State.ResolvedModule = SelectedModule;
	Data.Candidates.Empty();
	DistributionTracker.RecordSpawn(SelectedModule, CompiledBondingRules);

	// Log the selection with asset info
	const FString AssetName = CompiledBondingRules->ModuleAssets.IsValidIndex(SelectedModule)
		? CompiledBondingRules->ModuleAssets[SelectedModule].GetAssetName()
		: TEXT("Unknown");
	PCGEX_VALENCY_VERBOSE(Solver, "  CollapseState[%d]: SELECTED Module[%d] = '%s'", StateIndex, SelectedModule, *AssetName);

	// Propagate constraints to neighbors
	PropagateConstraints(StateIndex);

	return true;
}

void FPCGExValencyEntropySolver::PropagateConstraints(int32 ResolvedStateIndex)
{
	if (!ValencyStates->IsValidIndex(ResolvedStateIndex) || !OrbitalCache)
	{
		return;
	}

	// Filler modules have no neighbor data - skip propagation to avoid corrupting neighbor candidates
	const int32 ResolvedModule = (*ValencyStates)[ResolvedStateIndex].ResolvedModule;
	if (ResolvedModule >= 0 && CompiledBondingRules->IsModuleFiller(ResolvedModule))
	{
		return;
	}

	// For each orbital, notify the neighbor
	const int32 MaxOrbitals = GetMaxOrbitals();
	for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals; ++OrbitalIndex)
	{
		const int32 NeighborIndex = GetNeighborAtOrbital(ResolvedStateIndex, OrbitalIndex);
		if (NeighborIndex < 0 || !ValencyStates->IsValidIndex(NeighborIndex))
		{
			continue;
		}

		if ((*ValencyStates)[NeighborIndex].IsResolved())
		{
			continue;
		}

		// Update neighbor's entropy (more neighbors resolved = lower entropy)
		UpdateEntropy(NeighborIndex);
	}
}

bool FPCGExValencyEntropySolver::FilterCandidates(int32 StateIndex)
{
	if (!CompiledBondingRules || !ValencyStates->IsValidIndex(StateIndex) || !OrbitalCache)
	{
		return false;
	}

	FWFCStateData& Data = StateData[StateIndex];

	int32 RemovedByDistribution = 0;
	int32 RemovedByNeighbor = 0;
	int32 RemovedByArcConsistency = 0;

	const int32 MaxOrbitals = GetMaxOrbitals();

	// First pass: filter by distribution and resolved neighbor constraints
	// These are hard constraints that must be respected
	for (int32 i = Data.Candidates.Num() - 1; i >= 0; --i)
	{
		const int32 CandidateModule = Data.Candidates[i];

		// Check distribution constraints
		if (!DistributionTracker.CanSpawn(CandidateModule))
		{
			PCGEX_VALENCY_VERBOSE(Solver, "    FilterCandidates: Module[%d] rejected by distribution constraints", CandidateModule);
			Data.Candidates.RemoveAt(i);
			RemovedByDistribution++;
			continue;
		}

		// Check compatibility with each resolved neighbor
		bool bCompatible = true;
		for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals && bCompatible; ++OrbitalIndex)
		{
			const int32 NeighborIndex = GetNeighborAtOrbital(StateIndex, OrbitalIndex);
			if (NeighborIndex < 0 || !ValencyStates->IsValidIndex(NeighborIndex))
			{
				continue;
			}

			const PCGExValency::FValencyState& NeighborState = (*ValencyStates)[NeighborIndex];
			if (!NeighborState.IsResolved() || NeighborState.ResolvedModule < 0)
			{
				continue; // Neighbor not resolved yet, no constraint
			}

			// Check if this candidate is compatible with the neighbor's resolved module
			if (!IsModuleCompatibleWithNeighbor(CandidateModule, OrbitalIndex, NeighborState.ResolvedModule))
			{
				PCGEX_VALENCY_VERBOSE(Solver, "    FilterCandidates: Module[%d] incompatible with neighbor Module[%d] at orbital %d",
					CandidateModule, NeighborState.ResolvedModule, OrbitalIndex);
				bCompatible = false;
			}
		}

		if (!bCompatible)
		{
			Data.Candidates.RemoveAt(i);
			RemovedByNeighbor++;
		}
	}

	// Second pass: arc consistency (soft constraint - skip if it's our last option)
	// If we only have one candidate left, we should try it rather than giving up
	if (Data.Candidates.Num() > 1)
	{
		for (int32 i = Data.Candidates.Num() - 1; i >= 0; --i)
		{
			// Don't remove the last candidate via arc consistency - let it try
			if (Data.Candidates.Num() <= 1)
			{
				break;
			}

			const int32 CandidateModule = Data.Candidates[i];

			// Arc consistency check: would selecting this candidate leave any unresolved neighbor with zero candidates?
			if (!CheckArcConsistency(StateIndex, CandidateModule))
			{
				PCGEX_VALENCY_VERBOSE(Solver, "    FilterCandidates: Module[%d] rejected by arc consistency (would leave neighbor with no candidates)", CandidateModule);
				Data.Candidates.RemoveAt(i);
				RemovedByArcConsistency++;
			}
		}
	}
	else if (Data.Candidates.Num() == 1)
	{
		// Log that we're keeping the last candidate even if arc consistency fails
		const int32 LastCandidate = Data.Candidates[0];
		if (!CheckArcConsistency(StateIndex, LastCandidate))
		{
			PCGEX_VALENCY_VERBOSE(Solver, "    FilterCandidates: Module[%d] would fail arc consistency but keeping it (last candidate)", LastCandidate);
		}
	}

	if (RemovedByDistribution > 0 || RemovedByNeighbor > 0 || RemovedByArcConsistency > 0)
	{
		PCGEX_VALENCY_VERBOSE(Solver, "    FilterCandidates[%d]: Removed %d by distribution, %d by neighbor, %d by arc consistency",
			StateIndex, RemovedByDistribution, RemovedByNeighbor, RemovedByArcConsistency);
	}

	return Data.Candidates.Num() > 0;
}

bool FPCGExValencyEntropySolver::CheckArcConsistency(int32 StateIndex, int32 CandidateModule) const
{
	if (!CompiledBondingRules || !ValencyStates->IsValidIndex(StateIndex) || !OrbitalCache)
	{
		return false;
	}

	const int32 MaxOrbitals = OrbitalCache->GetMaxOrbitals();

	// For each unresolved neighbor, check if at least one of their candidates would be compatible
	for (int32 OrbitalIndex = 0; OrbitalIndex < MaxOrbitals; ++OrbitalIndex)
	{
		const int32 NeighborIndex = OrbitalCache->GetNeighborAtOrbital(StateIndex, OrbitalIndex);
		if (NeighborIndex < 0 || !ValencyStates->IsValidIndex(NeighborIndex))
		{
			continue;
		}

		const PCGExValency::FValencyState& NeighborState = (*ValencyStates)[NeighborIndex];
		if (NeighborState.IsResolved())
		{
			continue; // Already resolved, no need to check
		}

		const FWFCStateData& NeighborData = StateData[NeighborIndex];
		if (NeighborData.Candidates.Num() == 0)
		{
			continue; // Already empty (will be marked unsolvable elsewhere)
		}

		// Find which orbital of the neighbor points back to us
		int32 ReverseOrbitalIndex = -1;
		for (int32 NeighborOrbital = 0; NeighborOrbital < MaxOrbitals; ++NeighborOrbital)
		{
			if (OrbitalCache->GetNeighborAtOrbital(NeighborIndex, NeighborOrbital) == StateIndex)
			{
				ReverseOrbitalIndex = NeighborOrbital;
				break;
			}
		}

		if (ReverseOrbitalIndex < 0)
		{
			continue; // No reverse connection (unusual but possible)
		}

		// Check if any of the neighbor's candidates would be compatible with our candidate
		bool bHasCompatibleNeighborCandidate = false;
		for (int32 NeighborCandidate : NeighborData.Candidates)
		{
			// The neighbor candidate must accept our candidate module at its reverse orbital
			if (IsModuleCompatibleWithNeighbor(NeighborCandidate, ReverseOrbitalIndex, CandidateModule))
			{
				bHasCompatibleNeighborCandidate = true;
				break;
			}
		}

		if (!bHasCompatibleNeighborCandidate)
		{
			// Selecting this candidate would leave this neighbor with no valid candidates
			return false;
		}
	}

	return true;
}

// SelectWeightedRandom now inherited from base class FPCGExValencySolverOperation

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
