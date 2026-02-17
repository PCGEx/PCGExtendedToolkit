// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Growth/PCGExValencyGrowthOperation.h"

#include "Growth/Constraints/PCGExConstraintPreset.h"
#include "Growth/Constraints/PCGExConstraint_Branch.h"
#include "Helpers/PCGExStreamingHelpers.h"

#pragma region FPCGExValencyGrowthOperation

void FPCGExValencyGrowthOperation::Initialize(
	const FPCGExValencyBondingRulesCompiled* InCompiledRules,
	const UPCGExValencyConnectorSet* InConnectorSet,
	FPCGExBoundsTracker& InBoundsTracker,
	FPCGExGrowthBudget& InBudget,
	int32 InSeed)
{
	CompiledRules = InCompiledRules;
	ConnectorSet = InConnectorSet;
	BoundsTracker = &InBoundsTracker;
	Budget = &InBudget;
	RandomStream.Initialize(InSeed);
	DistributionTracker.Initialize(CompiledRules);
	ConstraintResolver.MaxCandidates = InBudget.MaxCandidatesPerConnector;

	BuildConstraintCache();
}

void FPCGExValencyGrowthOperation::Grow(TArray<FPCGExPlacedModule>& OutPlaced)
{
	if (!CompiledRules || !ConnectorSet || !BoundsTracker || !Budget) { return; }

	// Build initial frontier from seed modules' connectors
	TArray<FPCGExOpenConnector> Frontier;

	for (int32 PlacedIdx = 0; PlacedIdx < OutPlaced.Num(); ++PlacedIdx)
	{
		const FPCGExPlacedModule& Placed = OutPlaced[PlacedIdx];

		// Dead-end modules don't expand
		if (CompiledRules->ModuleIsDeadEnd[Placed.ModuleIndex]) { continue; }

		// Expand all connectors (seeds have no "used" connector)
		ExpandFrontier(Placed, PlacedIdx, -1, Frontier);
	}

	// Growth loop
	while (Frontier.Num() > 0 && Budget->CanPlaceMore())
	{
		const int32 SelectedIdx = SelectNextConnector(Frontier);
		if (SelectedIdx == INDEX_NONE) { break; }

		const FPCGExOpenConnector Connector = Frontier[SelectedIdx];
		Frontier.RemoveAtSwap(SelectedIdx);

		// Check depth budget
		if (!Budget->CanGrowDeeper(Connector.Depth + 1)) { continue; }

		// Find compatible modules for this connector type
		TArray<int32> CandidateModules;
		TArray<int32> CandidateConnectorIndices;
		FindCompatibleModules(Connector.ConnectorType, Connector.Polarity, CandidateModules, CandidateConnectorIndices);

		if (CandidateModules.IsEmpty()) { continue; }

		// Shuffle candidates for variety (Fisher-Yates)
		for (int32 i = CandidateModules.Num() - 1; i > 0; --i)
		{
			const int32 j = RandomStream.RandRange(0, i);
			CandidateModules.Swap(i, j);
			CandidateConnectorIndices.Swap(i, j);
		}

		// Try candidates in weighted-random order
		bool bPlaced = false;
		TArray<int32> WeightedOrder;
		WeightedOrder.Reserve(CandidateModules.Num());
		for (int32 i = 0; i < CandidateModules.Num(); ++i) { WeightedOrder.Add(i); }

		// Sort by weight (descending) with randomization
		WeightedOrder.Sort([&](int32 A, int32 B)
		{
			const float WeightA = CompiledRules->ModuleWeights[CandidateModules[A]];
			const float WeightB = CompiledRules->ModuleWeights[CandidateModules[B]];
			// Add jitter for randomization
			return (WeightA + RandomStream.FRand() * 0.1f) > (WeightB + RandomStream.FRand() * 0.1f);
		});

		for (int32 OrderIdx : WeightedOrder)
		{
			const int32 ModuleIdx = CandidateModules[OrderIdx];
			const int32 ConnectorIdx = CandidateConnectorIndices[OrderIdx];

			// Check weight budget
			if (!Budget->CanAfford(Connector.CumulativeWeight, CompiledRules->ModuleWeights[ModuleIdx])) { continue; }

			// Check distribution constraints
			if (!DistributionTracker.CanSpawn(ModuleIdx)) { continue; }

			if (TryPlaceModule(Connector, ModuleIdx, ConnectorIdx, OutPlaced, Frontier))
			{
				bPlaced = true;

				// Re-add to frontier if multi-spawn connector has remaining capacity
				if (Connector.RemainingSpawns > 1)
				{
					FPCGExOpenConnector Respawned = Connector;
					Respawned.RemainingSpawns--;
					Frontier.Add(Respawned);
				}

				break;
			}
		}

		if (!bPlaced && Budget->bStopOnFirstFailure)
		{
			break;
		}
	}
}

void FPCGExValencyGrowthOperation::FindCompatibleModules(
	FName ConnectorType,
	EPCGExConnectorPolarity SourcePolarity,
	TArray<int32>& OutModuleIndices,
	TArray<int32>& OutConnectorIndices) const
{
	if (!CompiledRules || !ConnectorSet) { return; }

	// Find the connector type index in the rules
	const int32 SourceTypeIndex = ConnectorSet->FindConnectorTypeIndex(ConnectorType);
	if (SourceTypeIndex == INDEX_NONE) { return; }

	// Get the compatibility mask for this connector type
	const int64 CompatMask = ConnectorSet->GetCompatibilityMask(SourceTypeIndex);

	// Scan all modules for compatible connectors
	for (int32 ModuleIdx = 0; ModuleIdx < CompiledRules->ModuleCount; ++ModuleIdx)
	{
		const TConstArrayView<FPCGExValencyModuleConnector> Connectors = CompiledRules->GetModuleConnectors(ModuleIdx);

		for (int32 ConnectorIdx = 0; ConnectorIdx < Connectors.Num(); ++ConnectorIdx)
		{
			const FPCGExValencyModuleConnector& ModuleConnector = Connectors[ConnectorIdx];

			// Find this connector's type index
			const int32 TargetTypeIndex = ConnectorSet->FindConnectorTypeIndex(ModuleConnector.ConnectorType);
			if (TargetTypeIndex == INDEX_NONE) { continue; }

			// Check type compatibility via bitmask AND polarity compatibility
			if ((CompatMask & (1LL << TargetTypeIndex)) != 0 &&
				PCGExValencyConnector::ArePolaritiesCompatible(SourcePolarity, ModuleConnector.Polarity))
			{
				OutModuleIndices.Add(ModuleIdx);
				OutConnectorIndices.Add(ConnectorIdx);
			}
		}
	}
}

FTransform FPCGExValencyGrowthOperation::ComputeAttachmentTransform(
	const FPCGExOpenConnector& ParentConnector,
	const FTransform& ParentModuleWorld,
	int32 ChildModuleIndex,
	int32 ChildConnectorIndex) const
{
	// Get child connector's effective offset (local space)
	const TConstArrayView<FPCGExValencyModuleConnector> ChildConnectors = CompiledRules->GetModuleConnectors(ChildModuleIndex);
	check(ChildConnectors.IsValidIndex(ChildConnectorIndex));
	const FPCGExValencyModuleConnector& ChildConnector = ChildConnectors[ChildConnectorIndex];
	const FTransform ChildConnectorLocal = ChildConnector.GetEffectiveOffset(ConnectorSet);

	// Start from parent module rotation, then apply a minimal correction so the child's
	// attachment connector faces opposite to the parent connector direction.
	// This preserves the parent module's base orientation (yaw, twist) while tilting
	// the child along the connector axis. Uses quaternion math (no Euler angles, no gimbal lock).
	const FVector ParentConnWorldFwd = ParentConnector.WorldTransform.GetRotation().GetForwardVector();
	const FVector ChildConnLocalFwd = ChildConnectorLocal.GetRotation().GetForwardVector();

	const FVector DesiredChildConnWorldFwd = -ParentConnWorldFwd;
	const FVector CurrentChildConnWorldFwd = ParentModuleWorld.GetRotation().RotateVector(ChildConnLocalFwd);

	const FQuat CorrectionRot = FQuat::FindBetweenNormals(CurrentChildConnWorldFwd, DesiredChildConnWorldFwd);
	const FQuat ChildModuleRot = CorrectionRot * ParentModuleWorld.GetRotation();

	// Scale: propagate from parent connector world (includes module scale * connector local scale)
	const FVector ChildModuleScale = ParentConnector.WorldTransform.GetScale3D();

	// Position: parent connector world pos - child connector offset rotated into the new module frame
	const FVector ChildModulePos = ParentConnector.WorldTransform.GetTranslation()
		- ChildModuleRot.RotateVector(ChildConnectorLocal.GetTranslation());

	return FTransform(ChildModuleRot, ChildModulePos, ChildModuleScale);
}

FBox FPCGExValencyGrowthOperation::ComputeWorldBounds(int32 ModuleIndex, const FTransform& WorldTransform) const
{
	FBox LocalBounds = ModuleLocalBounds[ModuleIndex];

	// Apply bounds modifier
	const FPCGExBoundsModifier& Modifier = CompiledRules->ModuleBoundsModifiers[ModuleIndex];
	LocalBounds = Modifier.Apply(LocalBounds);

	return LocalBounds.TransformBy(WorldTransform);
}

bool FPCGExValencyGrowthOperation::TryPlaceModule(
	const FPCGExOpenConnector& Connector,
	int32 ModuleIndex,
	int32 ChildConnectorIndex,
	TArray<FPCGExPlacedModule>& OutPlaced,
	TArray<FPCGExOpenConnector>& OutFrontier)
{
	const TConstArrayView<FPCGExValencyModuleConnector> ParentConnectors =
		CompiledRules->GetModuleConnectors(OutPlaced[Connector.PlacedModuleIndex].ModuleIndex);
	const FPCGExValencyModuleConnector& ParentMC = ParentConnectors[Connector.ConnectorIndex];

	const TConstArrayView<FPCGExValencyModuleConnector> ChildConnectors =
		CompiledRules->GetModuleConnectors(ModuleIndex);
	const FPCGExValencyModuleConnector& ChildMC = ChildConnectors[ChildConnectorIndex];

	// Build ordered constraint pipeline based on override modes
	TArray<const TArray<FInstancedStruct>*, TInlineAllocator<4>> ConstraintLists;

	auto AddConstraintLists = [&ConstraintLists, this](const FPCGExValencyModuleConnector& MC)
	{
		const int32 TypeIdx = ConnectorSet->FindConnectorTypeIndex(MC.ConnectorType);
		const TArray<FInstancedStruct>* Defaults =
			ConnectorSet->ConnectorTypes.IsValidIndex(TypeIdx)
				? &ConnectorSet->ConnectorTypes[TypeIdx].DefaultConstraints
				: nullptr;
		const bool bHasDefaults = Defaults && Defaults->Num() > 0;
		const bool bHasOverrides = MC.ConstraintOverrides.Num() > 0;

		if (bHasOverrides)
		{
			if (MC.OverrideMode == EPCGExConstraintOverrideMode::Append && bHasDefaults)
			{
				ConstraintLists.Add(Defaults);
			}
			ConstraintLists.Add(&MC.ConstraintOverrides);
		}
		else if (bHasDefaults)
		{
			ConstraintLists.Add(Defaults);
		}
	};

	AddConstraintLists(ParentMC);
	AddConstraintLists(ChildMC);

	const FTransform& ParentModuleWorld = OutPlaced[Connector.PlacedModuleIndex].WorldTransform;

	// Fast path: no constraints on either side -> single transform (zero overhead)
	if (ConstraintLists.IsEmpty())
	{
		const FTransform WorldTransform = ComputeAttachmentTransform(Connector, ParentModuleWorld, ModuleIndex, ChildConnectorIndex);

		return TryPlaceModuleAt(Connector, ModuleIndex, ChildConnectorIndex, WorldTransform, OutPlaced, OutFrontier);
	}

	// Build context
	const FTransform BaseTransform = ComputeAttachmentTransform(Connector, ParentModuleWorld, ModuleIndex, ChildConnectorIndex);

	FPCGExConstraintContext ConstraintContext;
	ConstraintContext.ParentConnectorWorld = Connector.WorldTransform;
	ConstraintContext.BaseAttachment = BaseTransform;
	ConstraintContext.ChildConnectorLocal = ChildMC.GetEffectiveOffset(ConnectorSet);
	ConstraintContext.OpenConnector = &Connector;
	ConstraintContext.ChildModuleIndex = ModuleIndex;
	ConstraintContext.ChildConnectorIndex = ChildConnectorIndex;
	ConstraintContext.Depth = Connector.Depth;
	ConstraintContext.CumulativeWeight = Connector.CumulativeWeight;
	ConstraintContext.PlacedCount = Budget->CurrentTotal;

	// Seed deterministic random for this specific connector evaluation
	// Advance the main stream to produce a unique seed per call (GetCurrentSeed() is constant)
	FRandomStream ConstraintRandom(
		RandomStream.GetUnsignedInt() ^ (static_cast<uint32>(Budget->CurrentTotal) << 16) ^ static_cast<uint32>(Connector.ConnectorIndex));

	// Resolve candidates through ordered pipeline (uses pre-cached pointer arrays)
	TArray<FTransform> Candidates;
	ConstraintResolver.Resolve(ConstraintContext, ConstraintLists, ConstraintRandom, Candidates);

	// Try each candidate until one fits
	for (const FTransform& CandidateTransform : Candidates)
	{
		if (TryPlaceModuleAt(Connector, ModuleIndex, ChildConnectorIndex, CandidateTransform, OutPlaced, OutFrontier))
		{
			return true;
		}
	}

	return false;
}

bool FPCGExValencyGrowthOperation::TryPlaceModuleAt(
	const FPCGExOpenConnector& Connector,
	int32 ModuleIndex,
	int32 ChildConnectorIndex,
	const FTransform& WorldTransform,
	TArray<FPCGExPlacedModule>& OutPlaced,
	TArray<FPCGExOpenConnector>& OutFrontier)
{
	// Compute world bounds
	const FBox WorldBounds = ComputeWorldBounds(ModuleIndex, WorldTransform);

	// Run placement condition stack (if module has conditions)
	const TConstArrayView<FInstancedStruct> Conditions =
		CompiledRules->GetModulePlacementConditions(ModuleIndex);

	if (Conditions.Num() > 0)
	{
		FPCGExPlacementContext PlacementCtx;
		PlacementCtx.WorldBounds = WorldBounds;
		PlacementCtx.WorldTransform = WorldTransform;
		PlacementCtx.ModuleIndex = ModuleIndex;
		PlacementCtx.Depth = Connector.Depth + 1;
		PlacementCtx.CumulativeWeight = Connector.CumulativeWeight;
		PlacementCtx.PlacedCount = Budget->CurrentTotal;
		PlacementCtx.BoundsTracker = BoundsTracker;
		PlacementCtx.CompiledRules = CompiledRules;

		for (const FInstancedStruct& Instance : Conditions)
		{
			if (const FPCGExPlacementCondition* Cond =
				Instance.GetPtr<FPCGExPlacementCondition>())
			{
				if (!Cond->Evaluate(PlacementCtx)) { return false; }
			}
		}
	}
	// Empty stack = no validation (module places unconditionally)

	// Place the module
	FPCGExPlacedModule NewModule;
	NewModule.ModuleIndex = ModuleIndex;
	NewModule.WorldTransform = WorldTransform;
	NewModule.WorldBounds = WorldBounds;
	NewModule.ParentIndex = Connector.PlacedModuleIndex;
	NewModule.ParentConnectorIndex = Connector.ConnectorIndex;
	NewModule.ChildConnectorIndex = ChildConnectorIndex;
	NewModule.Depth = Connector.Depth + 1;
	NewModule.SeedIndex = OutPlaced.IsValidIndex(Connector.PlacedModuleIndex) ? OutPlaced[Connector.PlacedModuleIndex].SeedIndex : 0;
	NewModule.CumulativeWeight = Connector.CumulativeWeight + CompiledRules->ModuleWeights[ModuleIndex];

	const int32 NewIndex = OutPlaced.Num();
	OutPlaced.Add(NewModule);

	// Track bounds and distribution
	if (WorldBounds.IsValid)
	{
		BoundsTracker->Add(WorldBounds);
	}
	Budget->CurrentTotal++;
	DistributionTracker.RecordSpawn(ModuleIndex, CompiledRules);

	// Expand frontier (unless dead-end)
	if (!CompiledRules->ModuleIsDeadEnd[ModuleIndex])
	{
		ExpandFrontier(NewModule, NewIndex, ChildConnectorIndex, OutFrontier);
	}

	return true;
}

void FPCGExValencyGrowthOperation::ExpandFrontier(
	const FPCGExPlacedModule& Placed,
	int32 PlacedIndex,
	int32 UsedConnectorIndex,
	TArray<FPCGExOpenConnector>& OutFrontier)
{
	const TConstArrayView<FPCGExValencyModuleConnector> Connectors = CompiledRules->GetModuleConnectors(Placed.ModuleIndex);

	for (int32 ConnectorIdx = 0; ConnectorIdx < Connectors.Num(); ++ConnectorIdx)
	{
		// Skip the connector that was used for attachment
		if (ConnectorIdx == UsedConnectorIndex) { continue; }

		const FPCGExValencyModuleConnector& ModuleConnector = Connectors[ConnectorIdx];

		// Compute world-space connector transform
		const FTransform ConnectorLocal = ModuleConnector.GetEffectiveOffset(ConnectorSet);
		const FTransform ConnectorWorld = ConnectorLocal * Placed.WorldTransform;

		FPCGExOpenConnector OpenConnector;
		OpenConnector.PlacedModuleIndex = PlacedIndex;
		OpenConnector.ConnectorIndex = ConnectorIdx;
		OpenConnector.ConnectorType = ModuleConnector.ConnectorType;
		OpenConnector.Polarity = ModuleConnector.Polarity;
		OpenConnector.WorldTransform = ConnectorWorld;
		OpenConnector.Depth = Placed.Depth;
		OpenConnector.CumulativeWeight = Placed.CumulativeWeight;
		OpenConnector.Priority = ModuleConnector.Priority;
		OpenConnector.RemainingSpawns = ModuleConnector.SpawnCapacity;

		OutFrontier.Add(OpenConnector);
	}
}

int32 FPCGExValencyGrowthOperation::SelectWeightedRandom(const TArray<int32>& CandidateModules)
{
	if (CandidateModules.IsEmpty()) { return INDEX_NONE; }
	if (CandidateModules.Num() == 1) { return 0; }

	float TotalWeight = 0.0f;
	for (int32 ModuleIdx : CandidateModules)
	{
		TotalWeight += CompiledRules->ModuleWeights[ModuleIdx];
	}

	if (TotalWeight <= 0.0f) { return 0; }

	float Pick = RandomStream.FRand() * TotalWeight;
	for (int32 i = 0; i < CandidateModules.Num(); ++i)
	{
		Pick -= CompiledRules->ModuleWeights[CandidateModules[i]];
		if (Pick <= 0.0f) { return i; }
	}

	return CandidateModules.Num() - 1;
}

void FPCGExValencyGrowthOperation::BuildConstraintCache()
{
	if (!CompiledRules || !ConnectorSet) { return; }

	// Phase 1: Collect and batch pre-load all soft references to constraint presets
	TSet<FSoftObjectPath> PresetPaths;

	auto CollectPresetPaths = [&PresetPaths](const TArray<FInstancedStruct>& Constraints)
	{
		for (const FInstancedStruct& Instance : Constraints)
		{
			const FPCGExConnectorConstraint* Constraint = Instance.GetPtr<FPCGExConnectorConstraint>();
			if (!Constraint) { continue; }

			if (Constraint->GetRole() == EPCGExConstraintRole::Preset)
			{
				const auto* PresetConstraint = static_cast<const FPCGExConstraint_Preset*>(Constraint);
				if (!PresetConstraint->Preset.IsNull())
				{
					PresetPaths.Add(PresetConstraint->Preset.ToSoftObjectPath());
				}
			}
			else if (Constraint->GetRole() == EPCGExConstraintRole::Branch)
			{
				const auto* BranchConstraint = static_cast<const FPCGExConstraint_Branch*>(Constraint);
				if (!BranchConstraint->OnPass.IsNull())
				{
					PresetPaths.Add(BranchConstraint->OnPass.ToSoftObjectPath());
				}
				if (!BranchConstraint->OnFail.IsNull())
				{
					PresetPaths.Add(BranchConstraint->OnFail.ToSoftObjectPath());
				}
			}
		}
	};

	// Scan both type-level defaults and per-instance overrides for preset references
	for (const FPCGExValencyConnectorEntry& Entry : ConnectorSet->ConnectorTypes)
	{
		CollectPresetPaths(Entry.DefaultConstraints);
	}

	for (int32 ModuleIdx = 0; ModuleIdx < CompiledRules->ModuleCount; ++ModuleIdx)
	{
		const TConstArrayView<FPCGExValencyModuleConnector> Connectors = CompiledRules->GetModuleConnectors(ModuleIdx);
		for (const FPCGExValencyModuleConnector& MC : Connectors)
		{
			if (MC.ConstraintOverrides.Num() > 0)
			{
				CollectPresetPaths(MC.ConstraintOverrides);
			}
		}
	}

	if (!PresetPaths.IsEmpty())
	{
		const TSharedPtr<TSet<FSoftObjectPath>> PathsPtr = MakeShared<TSet<FSoftObjectPath>>(MoveTemp(PresetPaths));
		PCGExHelpers::LoadBlocking_AnyThread(PathsPtr);
	}

	// Phase 2: Build pointer cache for all constraint arrays independently
	for (const FPCGExValencyConnectorEntry& Entry : ConnectorSet->ConnectorTypes)
	{
		ConstraintResolver.CacheConstraintList(Entry.DefaultConstraints);
	}

	for (int32 ModuleIdx = 0; ModuleIdx < CompiledRules->ModuleCount; ++ModuleIdx)
	{
		const TConstArrayView<FPCGExValencyModuleConnector> Connectors = CompiledRules->GetModuleConnectors(ModuleIdx);
		for (const FPCGExValencyModuleConnector& MC : Connectors)
		{
			if (MC.ConstraintOverrides.Num() > 0)
			{
				ConstraintResolver.CacheConstraintList(MC.ConstraintOverrides);
			}
		}
	}
}

#pragma endregion
