// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExValencySolverOperation.h"
#include "Core/PCGExValencyLog.h"
#include "Data/PCGExData.h"

void UPCGExValencySolverInstancedFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
}

void UPCGExValencySolverInstancedFactory::RegisterPrimaryBuffersDependencies(
	FPCGExContext* InContext,
	PCGExData::FFacadePreloader& FacadePreloader) const
{
	// Default: no additional buffer dependencies
}

TSharedPtr<PCGExValency::FSolverAllocations> UPCGExValencySolverInstancedFactory::CreateAllocations(
	const TSharedRef<PCGExData::FFacade>& VtxFacade) const
{
	// Default: no allocations needed
	return nullptr;
}

namespace PCGExValency
{
	void FDistributionTracker::Initialize(const FPCGExValencyBondingRulesCompiled* CompiledBondingRules)
	{
		if (!CompiledBondingRules)
		{
			return;
		}

		SpawnCounts.SetNumZeroed(CompiledBondingRules->ModuleCount);
		ModulesNeedingMinimum.Empty();
		ModulesAtMaximum.Empty();

		// Identify modules with minimum spawn requirements (skip Excluded modules)
		for (int32 i = 0; i < CompiledBondingRules->ModuleCount; ++i)
		{
			if (CompiledBondingRules->IsModuleExcluded(i)) { continue; }

			if (CompiledBondingRules->ModuleMinSpawns[i] > 0)
			{
				ModulesNeedingMinimum.Add(i);
			}
		}
	}

	bool FDistributionTracker::RecordSpawn(int32 ModuleIndex, const FPCGExValencyBondingRulesCompiled* CompiledBondingRules)
	{
		if (!CompiledBondingRules || !SpawnCounts.IsValidIndex(ModuleIndex))
		{
			return false;
		}

		// Check if already at maximum
		const int32 MaxSpawns = CompiledBondingRules->ModuleMaxSpawns[ModuleIndex];
		if (MaxSpawns >= 0 && SpawnCounts[ModuleIndex] >= MaxSpawns)
		{
			return false;
		}

		// Record spawn
		SpawnCounts[ModuleIndex]++;

		// Check if minimum is now satisfied
		const int32 MinSpawns = CompiledBondingRules->ModuleMinSpawns[ModuleIndex];
		if (SpawnCounts[ModuleIndex] >= MinSpawns)
		{
			ModulesNeedingMinimum.Remove(ModuleIndex);
		}

		// Check if maximum is now reached
		if (MaxSpawns >= 0 && SpawnCounts[ModuleIndex] >= MaxSpawns)
		{
			ModulesAtMaximum.Add(ModuleIndex);
		}

		return true;
	}

	bool FDistributionTracker::CanSpawn(int32 ModuleIndex) const
	{
		return !ModulesAtMaximum.Contains(ModuleIndex);
	}

	void FSlotBudget::Initialize(
		const FPCGExValencyBondingRulesCompiled* Rules,
		const TArray<FValencyState>& States,
		const FOrbitalCache* Cache,
		FModuleFitChecker FitChecker)
	{
		if (!Rules || !Cache)
		{
			return;
		}

		const int32 NumNodes = Cache->GetNumNodes();

		// Initialize available slots per module
		AvailableSlots.SetNumZeroed(Rules->ModuleCount);

		// Initialize state-to-modules mapping
		StateToFittingModules.SetNum(NumNodes);

		// For each node, find which modules fit
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const FValencyState& State = States[NodeIndex];
			TArray<int32>& FittingModules = StateToFittingModules[NodeIndex];
			FittingModules.Empty();

			// Skip already resolved states (boundaries)
			if (State.IsResolved())
			{
				continue;
			}

			// Find all modules that fit this node (skip Excluded modules)
			for (int32 ModuleIndex = 0; ModuleIndex < Rules->ModuleCount; ++ModuleIndex)
			{
				if (Rules->IsModuleExcluded(ModuleIndex)) { continue; }

				if (FitChecker(ModuleIndex, NodeIndex))
				{
					FittingModules.Add(ModuleIndex);
					AvailableSlots[ModuleIndex]++;
				}
			}
		}

		PCGEX_VALENCY_VERBOSE(Solver, "SlotBudget initialized: %d modules, %d nodes", Rules->ModuleCount, NumNodes);
		for (int32 i = 0; i < Rules->ModuleCount; ++i)
		{
			if (Rules->ModuleMinSpawns[i] > 0)
			{
				PCGEX_VALENCY_VERBOSE(Solver, "  Module[%d]: MinSpawns=%d, AvailableSlots=%d",
					i, Rules->ModuleMinSpawns[i], AvailableSlots[i]);
			}
		}
	}

	void FSlotBudget::OnStateCollapsed(int32 StateIndex)
	{
		if (!StateToFittingModules.IsValidIndex(StateIndex))
		{
			return;
		}

		// Decrement available slots for all modules that could have fit this state
		for (int32 ModuleIndex : StateToFittingModules[StateIndex])
		{
			if (AvailableSlots.IsValidIndex(ModuleIndex) && AvailableSlots[ModuleIndex] > 0)
			{
				AvailableSlots[ModuleIndex]--;
			}
		}

		// Clear the fitting modules list (state is now collapsed)
		StateToFittingModules[StateIndex].Empty();
	}

	float FSlotBudget::GetUrgency(
		int32 ModuleIndex,
		const FDistributionTracker& Tracker,
		const FPCGExValencyBondingRulesCompiled* Rules) const
	{
		if (!Rules || !Rules->ModuleMinSpawns.IsValidIndex(ModuleIndex))
		{
			return 0.0f;
		}

		const int32 MinSpawns = Rules->ModuleMinSpawns[ModuleIndex];
		if (MinSpawns <= 0)
		{
			return 0.0f; // No minimum constraint
		}

		const int32 CurrentSpawns = Tracker.SpawnCounts.IsValidIndex(ModuleIndex) ? Tracker.SpawnCounts[ModuleIndex] : 0;
		const int32 RequiredSpawns = MinSpawns - CurrentSpawns;

		if (RequiredSpawns <= 0)
		{
			return 0.0f; // Minimum already satisfied
		}

		const int32 Available = AvailableSlots.IsValidIndex(ModuleIndex) ? AvailableSlots[ModuleIndex] : 0;

		if (Available <= 0)
		{
			return TNumericLimits<float>::Max(); // Impossible - no slots left but still need spawns
		}

		// Urgency = required / available
		// 0.5 = need half the remaining slots
		// 1.0 = need ALL remaining slots (must select now)
		// >1.0 = impossible (need more than available)
		return static_cast<float>(RequiredSpawns) / static_cast<float>(Available);
	}

	int32 FSlotBudget::GetForcedSelection(
		const TArray<int32>& Candidates,
		const FDistributionTracker& Tracker,
		const FPCGExValencyBondingRulesCompiled* Rules) const
	{
		int32 MostUrgentModule = -1;
		float HighestUrgency = 0.0f;

		for (int32 ModuleIndex : Candidates)
		{
			const float Urgency = GetUrgency(ModuleIndex, Tracker, Rules);

			if (Urgency >= 1.0f && Urgency > HighestUrgency)
			{
				HighestUrgency = Urgency;
				MostUrgentModule = ModuleIndex;
			}
		}

		return MostUrgentModule;
	}

	bool FSlotBudget::AreConstraintsSatisfiable(
		const FDistributionTracker& Tracker,
		const FPCGExValencyBondingRulesCompiled* Rules) const
	{
		if (!Rules)
		{
			return true;
		}

		for (int32 ModuleIndex : Tracker.ModulesNeedingMinimum)
		{
			const float Urgency = GetUrgency(ModuleIndex, Tracker, Rules);
			if (Urgency > 1.0f)
			{
				// This module needs more spawns than available slots - impossible
				return false;
			}
		}

		return true;
	}
}

void FPCGExValencySolverOperation::Initialize(
	const FPCGExValencyBondingRulesCompiled* InCompiledBondingRules,
	TArray<PCGExValency::FValencyState>& InValencyStates,
	const PCGExValency::FOrbitalCache* InOrbitalCache,
	int32 InSeed,
	const TSharedPtr<PCGExValency::FSolverAllocations>& InAllocations)
{
	CompiledBondingRules = InCompiledBondingRules;
	ValencyStates = &InValencyStates;
	OrbitalCache = InOrbitalCache;
	Allocations = InAllocations;
	RandomStream.Initialize(InSeed);

	DistributionTracker.Initialize(CompiledBondingRules);

	// Initialize filler candidates storage
	FillerCandidatesPerState.SetNum(InValencyStates.Num());

	// Mark boundary states (no orbitals = NULL_SLOT)
	if (OrbitalCache)
	{
		for (int32 NodeIndex = 0; NodeIndex < ValencyStates->Num(); ++NodeIndex)
		{
			if (!OrbitalCache->HasOrbitals(NodeIndex))
			{
				(*ValencyStates)[NodeIndex].ResolvedModule = PCGExValency::SlotState::NULL_SLOT;
			}
		}
	}
}

int32 FPCGExValencySolverOperation::SweepFillers()
{
	int32 FillerCount = 0;

	for (int32 i = 0; i < ValencyStates->Num(); ++i)
	{
		PCGExValency::FValencyState& State = (*ValencyStates)[i];
		if (State.IsResolved()) { continue; }
		if (!HasOrbitals(i)) { continue; }

		TArray<int32>& Fillers = FillerCandidatesPerState[i];
		if (Fillers.Num() == 0)
		{
			State.ResolvedModule = PCGExValency::SlotState::UNSOLVABLE;
			continue;
		}

		const int32 SelectedFiller = SelectWeightedRandom(Fillers);
		if (SelectedFiller >= 0)
		{
			State.ResolvedModule = SelectedFiller;
			DistributionTracker.RecordSpawn(SelectedFiller, CompiledBondingRules);
			Fillers.Empty();
			FillerCount++;

			PCGEX_VALENCY_VERBOSE(Solver, "  Filler sweep: State[%d] assigned FILLER Module[%d]", i, SelectedFiller);
		}
		else
		{
			State.ResolvedModule = PCGExValency::SlotState::UNSOLVABLE;
		}
	}

	if (FillerCount > 0)
	{
		PCGEX_VALENCY_INFO(Solver, "Filler sweep assigned %d filler modules", FillerCount);
	}

	return FillerCount;
}

bool FPCGExValencySolverOperation::IsModuleCompatibleWithNeighbor(int32 ModuleIndex, int32 OrbitalIndex, int32 NeighborModuleIndex) const
{
	if (!CompiledBondingRules)
	{
		return false;
	}

	const FPCGExValencyLayerCompiled& Layer = CompiledBondingRules->Layer;

	// First try the explicit neighbor list
	if (Layer.OrbitalAcceptsNeighbor(ModuleIndex, OrbitalIndex, NeighborModuleIndex))
	{
		return true;
	}

	// If orbital has no defined neighbors, check if it's a wildcard or boundary:
	// - Null cage connection (boundary orbital) = must have NO neighbor → return false
	// - No connection at all (wildcard) = accepts ANY neighbor → return true
	const int32 HeaderIndex = ModuleIndex * Layer.OrbitalCount + OrbitalIndex;
	if (Layer.NeighborHeaders.IsValidIndex(HeaderIndex))
	{
		const FIntPoint& Header = Layer.NeighborHeaders[HeaderIndex];
		if (Header.Y == 0) // Empty neighbor list
		{
			// Check if this is a boundary orbital (connected to null cage)
			const int64 BoundaryMask = CompiledBondingRules->GetModuleBoundaryMask(ModuleIndex);
			const bool bIsBoundary = (BoundaryMask & (1LL << OrbitalIndex)) != 0;

			// If NOT a boundary, it's a wildcard - accept any neighbor
			if (!bIsBoundary)
			{
				return true;
			}
		}
	}

	return false;
}

bool FPCGExValencySolverOperation::DoesModuleFitNode(int32 ModuleIndex, int32 NodeIndex) const
{
	if (!CompiledBondingRules || !OrbitalCache)
	{
		return false;
	}

	// Get node's orbital mask from cache
	const int64 NodeMask = OrbitalCache->GetOrbitalMask(NodeIndex);

	const int64 ModuleMask = CompiledBondingRules->GetModuleOrbitalMask(ModuleIndex);
	const int64 BoundaryMask = CompiledBondingRules->GetModuleBoundaryMask(ModuleIndex);
	const int64 WildcardMask = CompiledBondingRules->GetModuleWildcardMask(ModuleIndex);

	// Module's required orbitals must be present in node
	if ((ModuleMask & NodeMask) != ModuleMask)
	{
		PCGEX_VALENCY_VERBOSE(Solver, "    Module[%d] REJECTED: ModuleMask=0x%llX, NodeMask=0x%llX, (ModuleMask & NodeMask)=0x%llX != ModuleMask",
			ModuleIndex, ModuleMask, NodeMask, (ModuleMask & NodeMask));
		return false;
	}

	// Module's boundary orbitals must NOT have connections in node
	if ((BoundaryMask & NodeMask) != 0)
	{
		PCGEX_VALENCY_VERBOSE(Solver, "    Module[%d] REJECTED: BoundaryMask=0x%llX conflicts with NodeMask=0x%llX",
			ModuleIndex, BoundaryMask, NodeMask);
		return false;
	}

	// Module's wildcard orbitals must HAVE connections in node
	if ((WildcardMask & NodeMask) != WildcardMask)
	{
		PCGEX_VALENCY_VERBOSE(Solver, "    Module[%d] REJECTED: WildcardMask=0x%llX requires neighbors, NodeMask=0x%llX missing some",
			ModuleIndex, WildcardMask, NodeMask);
		return false;
	}

	return true;
}

int32 FPCGExValencySolverOperation::SelectWeightedRandom(const TArray<int32>& Candidates)
{
	if (Candidates.Num() == 0)
	{
		return -1;
	}

	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}

	// Calculate total weight
	float TotalWeight = 0.0f;
	TArray<float> CumulativeWeights;
	CumulativeWeights.Reserve(Candidates.Num());

	// Prioritize modules that need more spawns to meet minimum
	const TSet<int32>& NeedingMinimum = DistributionTracker.GetModulesNeedingMinimum();

	for (int32 ModuleIndex : Candidates)
	{
		float Weight = CompiledBondingRules->ModuleWeights[ModuleIndex];

		// Boost weight for modules needing minimum spawns
		if (NeedingMinimum.Contains(ModuleIndex))
		{
			Weight *= MinimumSpawnWeightBoost;
		}

		TotalWeight += Weight;
		CumulativeWeights.Add(TotalWeight);
	}

	if (TotalWeight <= 0.0f)
	{
		// Fallback to uniform random if weights are bad
		return Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)];
	}

	// Weighted random selection
	const float RandomValue = RandomStream.FRand() * TotalWeight;

	for (int32 i = 0; i < CumulativeWeights.Num(); ++i)
	{
		if (RandomValue <= CumulativeWeights[i])
		{
			return Candidates[i];
		}
	}

	// Fallback (shouldn't reach here)
	return Candidates.Last();
}
