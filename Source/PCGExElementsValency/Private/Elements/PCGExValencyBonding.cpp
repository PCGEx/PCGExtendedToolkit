// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExValencyBonding.h"

#include "PCGParamData.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Containers/PCGExManagedObjects.h"
#include "Solvers/PCGExValencyEntropySolver.h"
#include "Core/PCGExValencyLog.h"

#define LOCTEXT_NAMESPACE "PCGExValencyBonding"
#define PCGEX_NAMESPACE ValencyBonding

void UPCGExValencyBondingSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Solver) { Solver = NewObject<UPCGExValencyEntropySolver>(this, TEXT("Solver")); }
	}
	Super::PostInitProperties();
}


TArray<FPCGPinProperties> UPCGExValencyBondingSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	// Valency Map pin is auto-added by base class via WantsValencyMap()
	if (bEnableFixedPicks) { PCGEX_PIN_FILTERS(PCGExValency::Labels::SourceFixedPickFiltersLabel, "Filters controlling which points are eligible for fixed picking.", Normal) }
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExValencyBondingSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_PARAMS(PCGExValency::Labels::OutputValencyMapLabel, "Valency map for resolving ValencyEntry hashes", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExValencyBondingSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Duplicate; // Duplicate since we're writing to vtx data
}

PCGExData::EIOInit UPCGExValencyBondingSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ValencyBonding)

FPCGElementPtr UPCGExValencyBondingSettings::CreateElement() const
{
	return MakeShared<FPCGExValencyBondingElement>();
}

bool FPCGExValencyBondingElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExValencyProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyBonding)

	PCGEX_OPERATION_VALIDATE(Solver)

	return true;
}

bool FPCGExValencyBondingElement::PostBoot(FPCGExContext* InContext) const
{
	// Base class: ConsumeValencyMap -> BondingRules, OrbitalSet, ConnectorSet, Suffix, MaxOrbitals
	if (!FPCGExValencyProcessorElement::PostBoot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyBonding)

	// Compile BondingRules if needed (ConsumeValencyMap already checked IsCompiled, but re-check in case)
	if (!Context->BondingRules->IsCompiled())
	{
		if (!Context->BondingRules->Compile())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to compile Valency Bonding Rules."));
			return false;
		}
	}

	// Register solver from settings
	Context->Solver = PCGEX_OPERATION_REGISTER_C(Context, UPCGExValencySolverInstancedFactory, Settings->Solver, NAME_None);
	if (!Context->Solver) { return false; }

	// Create valency packer seeded from input map (preserves hash keys for output duplication)
	Context->ValencyPacker = MakeShared<PCGExValency::FValencyPacker>(Context);
	Context->ValencyPacker->SeedFrom(*Context->ValencyUnpacker);

	// Get fixed pick filter factories if enabled (optional - empty array is valid)
	if (Settings->bEnableFixedPicks)
	{
		GetInputFactories(Context, PCGExValency::Labels::SourceFixedPickFiltersLabel, Context->FixedPickFilterFactories, PCGExFactories::ClusterNodeFilters, false);
	}

	return true;
}

bool FPCGExValencyBondingElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(ValencyBonding)

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				// Assign fixed pick filter factories to batch
				if (Settings->bEnableFixedPicks && !Context->FixedPickFilterFactories.IsEmpty())
				{
					static_cast<PCGExValencyBonding::FBatch*>(NewBatch.Get())->FixedPickFilterFactories = &Context->FixedPickFilterFactories;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	// Output valency map - duplicate input data to preserve all metadata
	for (const UPCGParamData* InputMap : Context->InputValencyMapData)
	{
		UPCGParamData* OutputMap = Context->ManagedObjects->DuplicateData<UPCGParamData>(InputMap);
		Context->StageOutput(OutputMap, PCGExValency::Labels::OutputValencyMapLabel, PCGExData::EStaging::None);
	}

	return Context->TryComplete();
}

namespace PCGExValencyBonding
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExValencyBonding::Process);

		if (!TProcessor::Process(InTaskManager)) { return false; }

		// Initialize and run fixed pick filters if we have factories
		if (FixedPickFilterFactories && !FixedPickFilterFactories->IsEmpty() && FixedPickFilterCache)
		{
			FixedPickFiltersManager = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
			FixedPickFiltersManager->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters);

			if (FixedPickFiltersManager->Init(ExecutionContext, *FixedPickFilterFactories))
			{
				// Run filters on all nodes to populate the cache
				const PCGExMT::FScope AllNodesScope(0, Cluster->Nodes->Num());
				FixedPickFiltersManager->Test(AllNodesScope.GetView(*Cluster->Nodes.Get()), *FixedPickFilterCache.Get(), true);
			}
		}

		// Apply fixed picks before solver runs (pre-resolve specified nodes)
		ApplyFixedPicks();

		// Run solver
		// BUG : Annotation are somehow broken when enabling local transform
		// TODO : Need to support wildcard for regular cages
		RunSolver();

		if (ValencyStates.IsEmpty()) { return false; }

		VALENCY_LOG_SECTION(Staging, "WRITING VALENCY RESULTS");

		if (!Context->BondingRules || !Context->BondingRules->IsCompiled())
		{
			PCGEX_VALENCY_ERROR(Staging, "FProcessor::Process Missing BondingRules or CompiledData!");
			return false;
		}

		// Process valency states in parallel
		StartParallelLoopForRange(ValencyStates.Num());

		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();

		PCGEX_SCOPE_LOOP(Index)
		{
			const PCGExValency::FValencyState& State = ValencyStates[Index];
			const int32 PointIndex = Nodes[State.NodeIndex].PointIndex;

			if (State.ResolvedModule >= 0)
			{
				FPlatformAtomics::InterlockedIncrement(&ResolvedCount);

				// Write ValencyEntry hash (encodes BondingRules identity + module index)
				if (ValencyEntryWriter && Context->ValencyPacker)
				{
					const uint64 Hash = Context->ValencyPacker->GetEntryIdx(
						Context->BondingRules, static_cast<uint16>(State.ResolvedModule));
					ValencyEntryWriter->SetValue(PointIndex, static_cast<int64>(Hash));
				}
			}
			else if (State.IsUnsolvable())
			{
				FPlatformAtomics::InterlockedIncrement(&UnsolvableCount);
			}
			else if (State.IsBoundary())
			{
				FPlatformAtomics::InterlockedIncrement(&BoundaryCount);
			}

			// Write unsolvable marker
			if (UnsolvableWriter)
			{
				UnsolvableWriter->SetValue(PointIndex, State.IsUnsolvable());
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		VALENCY_LOG_SECTION(Staging, "WRITE COMPLETE");
		PCGEX_VALENCY_INFO(Staging, "Resolved=%d, Unsolvable=%d, Boundary=%d", ResolvedCount, UnsolvableCount, BoundaryCount);
	}

	void FProcessor::ApplyFixedPicks()
	{
		// Skip if no fixed pick reader or no compiled data
		if (!FixedPickReader || !Context->BondingRules || !Context->BondingRules->IsCompiled())
		{
			return;
		}

		const FPCGExValencyBondingRulesCompiled* CompiledRules = Context->BondingRules->GetCompiledData();
		if (CompiledRules->ModuleCount == 0)
		{
			return;
		}

		VALENCY_LOG_SECTION(Staging, "APPLYING FIXED PICKS");

		// Build name to module indices map (once per processor)
		TMap<FName, TArray<int32>> NameToModules;
		for (int32 ModuleIndex = 0; ModuleIndex < CompiledRules->ModuleCount; ++ModuleIndex)
		{
			const FName& ModuleName = CompiledRules->ModuleNames[ModuleIndex];
			if (!ModuleName.IsNone())
			{
				NameToModules.FindOrAdd(ModuleName).Add(ModuleIndex);
			}
		}

		if (NameToModules.IsEmpty())
		{
			PCGEX_VALENCY_INFO(Staging, "No named modules found - skipping fixed picks");
			return;
		}

		PCGEX_VALENCY_INFO(Staging, "Found %d named module groups", NameToModules.Num());

		// Random stream for weighted selection (deterministic based on solver seed)
		int32 FixedPickSeed = Settings->Seed;
		if (Settings->bUsePerClusterSeed && Cluster)
		{
			FixedPickSeed = HashCombine(FixedPickSeed, GetTypeHash(VtxDataFacade->GetIn()->UID));
		}
		FRandomStream RandomStream(FixedPickSeed);

		int32 FixedPicksApplied = 0;
		int32 FixedPicksSkipped = 0;

		// Get cluster nodes for point index lookup
		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		// Apply fixed picks to states
		for (int32 StateIndex = 0; StateIndex < ValencyStates.Num(); ++StateIndex)
		{
			PCGExValency::FValencyState& State = ValencyStates[StateIndex];

			// Skip already resolved states (boundaries)
			if (State.IsResolved())
			{
				continue;
			}

			// Get the point index from cluster node
			const int32 PointIndex = Nodes[State.NodeIndex].PointIndex;

			// Read the fixed pick name for this node
			const FName PickName = FixedPickReader->Read(PointIndex);
			if (PickName.IsNone())
			{
				continue;
			}

			// Check FixedPickFilterCache if available (filter must pass for fixed pick to apply)
			if (FixedPickFilterCache && !(*FixedPickFilterCache)[PointIndex])
			{
				PCGEX_VALENCY_VERBOSE(Staging, "  State[%d]: Fixed pick '%s' skipped (filter failed)", StateIndex, *PickName.ToString());
				continue;
			}

			// Look up matching modules
			const TArray<int32>* MatchingModules = NameToModules.Find(PickName);
			if (!MatchingModules || MatchingModules->IsEmpty())
			{
				if (Settings->bWarnOnUnmatchedFixedPick)
				{
					PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
						           FTEXT("Fixed pick '{0}' on node {1} doesn't match any module name."),
						           FText::FromName(PickName), FText::AsNumber(StateIndex)));
				}
				FixedPicksSkipped++;
				continue;
			}

			// Filter by orbital fit and select the best module
			int32 SelectedModule = -1;
			TArray<int32> FittingModules;

			// Get node's orbital mask from cache
			const int64 NodeOrbitalMask = OrbitalCache ? OrbitalCache->GetOrbitalMask(State.NodeIndex) : 0;

			for (int32 ModuleIndex : *MatchingModules)
			{
				// Check if module fits the node's orbital configuration
				const int64 ModuleMask = CompiledRules->GetModuleOrbitalMask(ModuleIndex);
				const int64 ModuleBoundaryMask = CompiledRules->GetModuleBoundaryMask(ModuleIndex);

				// Module requires certain orbitals to be connected
				if ((ModuleMask & NodeOrbitalMask) != ModuleMask)
				{
					continue;
				}

				// Module requires certain orbitals to be disconnected (boundary)
				if ((ModuleBoundaryMask & NodeOrbitalMask) != 0)
				{
					continue;
				}

				FittingModules.Add(ModuleIndex);
			}

			// Handle no fitting modules
			if (FittingModules.IsEmpty())
			{
				if (Settings->IncompatibleFixedPickBehavior == EPCGExFixedPickIncompatibleBehavior::Force)
				{
					// Force: use first matching module regardless of fit
					FittingModules = *MatchingModules;
					PCGEX_VALENCY_VERBOSE(Staging, "  State[%d]: Forcing fixed pick '%s' (incompatible orbital config)", StateIndex, *PickName.ToString());
				}
				else
				{
					// Skip: let solver decide
					if (Settings->bWarnOnIncompatibleFixedPick)
					{
						PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(
							           FTEXT("Fixed pick '{0}' on node {1} doesn't fit orbital configuration - skipping."),
							           FText::FromName(PickName), FText::AsNumber(StateIndex)));
					}
					FixedPicksSkipped++;
					continue;
				}
			}

			// Select from fitting modules based on selection mode
			if (FittingModules.Num() == 1)
			{
				SelectedModule = FittingModules[0];
			}
			else
			{
				switch (Settings->FixedPickSelectionMode)
				{
				case EPCGExFixedPickSelectionMode::FirstMatch:
					SelectedModule = FittingModules[0];
					break;

				case EPCGExFixedPickSelectionMode::BestFit:
					{
						// Select module with most matching orbitals
						int32 BestScore = -1;
						for (int32 ModuleIndex : FittingModules)
						{
							const int64 ModuleMask = CompiledRules->GetModuleOrbitalMask(ModuleIndex);
							const int32 Score = FMath::CountBits(ModuleMask & NodeOrbitalMask);
							if (Score > BestScore)
							{
								BestScore = Score;
								SelectedModule = ModuleIndex;
							}
						}
					}
					break;

				case EPCGExFixedPickSelectionMode::WeightedRandom:
				default:
					{
						// Weighted random selection
						float TotalWeight = 0.0f;
						for (int32 ModuleIndex : FittingModules)
						{
							TotalWeight += CompiledRules->ModuleWeights[ModuleIndex];
						}

						if (TotalWeight > 0.0f)
						{
							float Pick = RandomStream.FRand() * TotalWeight;
							for (int32 ModuleIndex : FittingModules)
							{
								Pick -= CompiledRules->ModuleWeights[ModuleIndex];
								if (Pick <= 0.0f)
								{
									SelectedModule = ModuleIndex;
									break;
								}
							}
							// Fallback
							if (SelectedModule < 0)
							{
								SelectedModule = FittingModules.Last();
							}
						}
						else
						{
							// All weights zero, pick first
							SelectedModule = FittingModules[0];
						}
					}
					break;
				}
			}

			// Apply the fixed pick
			if (SelectedModule >= 0)
			{
				State.ResolvedModule = SelectedModule;
				FixedPicksApplied++;
				PCGEX_VALENCY_VERBOSE(Staging, "  State[%d]: Fixed pick '%s' -> Module[%d]", StateIndex, *PickName.ToString(), SelectedModule);
			}
		}

		PCGEX_VALENCY_INFO(Staging, "Fixed picks: %d applied, %d skipped", FixedPicksApplied, FixedPicksSkipped);
	}

	void FProcessor::RunSolver()
	{
		VALENCY_LOG_SECTION(Staging, "RUNNING VALENCY SOLVER");

		if (!Context->BondingRules || !Context->BondingRules->IsCompiled())
		{
			PCGEX_VALENCY_ERROR(Staging, "RunSolver: Missing BondingRules or CompiledData!");
			return;
		}

		PCGEX_VALENCY_INFO(Staging, "BondingRules: '%s', CompiledModules: %d",
		                   *Context->BondingRules->GetName(),
		                   Context->BondingRules->CompiledData.ModuleCount);

		// Create solver from factory
		if (Context->Solver)
		{
			Solver = Context->Solver->CreateOperation();
		}

		if (!Solver)
		{
			PCGE_LOG_C(Error, GraphAndLog, Context, FTEXT("Failed to create solver."));
			return;
		}

		// Calculate seed
		int32 SolveSeed = Settings->Seed;
		if (Settings->bUsePerClusterSeed && Cluster)
		{
			// Mix in cluster-specific data for variation
			SolveSeed = HashCombine(SolveSeed, GetTypeHash(VtxDataFacade->GetIn()->UID));
		}

		PCGEX_VALENCY_INFO(Staging, "Initializing solver with seed %d, %d states", SolveSeed, ValencyStates.Num());

		Solver->Initialize(Context->BondingRules->GetCompiledData(), ValencyStates, OrbitalCache.Get(), SolveSeed, SolverAllocations);
		SolveResult = Solver->Solve();

		VALENCY_LOG_SECTION(Staging, "SOLVER RESULT");
		PCGEX_VALENCY_INFO(Staging, "Resolved=%d, Unsolvable=%d, Boundary=%d, Success=%s",
		                   SolveResult.ResolvedCount, SolveResult.UnsolvableCount, SolveResult.BoundaryCount,
		                   SolveResult.bSuccess ? TEXT("true") : TEXT("false"));

		if (SolveResult.UnsolvableCount > 0)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FText::Format(FTEXT("Valency Solver: {0} nodes were unsolvable."), FText::AsNumber(SolveResult.UnsolvableCount)));
		}

		if (!SolveResult.MinimumsSatisfied)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Valency Solver: Minimum spawn constraints were not satisfied."));
		}
	}

	void FProcessor::Write()
	{
		TProcessor::Write();

		// Optionally prune unsolvable points
		if (Settings->bPruneUnsolvable)
		{
			TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
			TArray<int32> IndicesToRemove;

			for (const PCGExValency::FValencyState& State : ValencyStates)
			{
				if (State.IsUnsolvable())
				{
					const PCGExClusters::FNode& Node = Nodes[State.NodeIndex];
					IndicesToRemove.Add(Node.PointIndex);
				}
			}

			// Note: Actual point removal would need to be handled by the cluster system
			// This is a placeholder for the pruning logic
		}
	}

	//////// BATCH

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		PCGExValencyMT::IBatch::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ValencyBonding)

		// Let solver register its buffer dependencies (e.g., priority attribute)
		if (Context->Solver)
		{
			Context->Solver->RegisterPrimaryBuffersDependencies(Context, FacadePreloader);
		}

		// Register fixed pick attribute if enabled
		if (Settings->bEnableFixedPicks)
		{
			FacadePreloader.TryRegister(Context, Settings->FixedPickAttribute);
		}
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ValencyBonding)

		const TSharedRef<PCGExData::FFacade>& OutputFacade = VtxDataFacade;

		// Create solver allocations (buffers are now preloaded)
		if (Context->Solver)
		{
			SolverAllocations = Context->Solver->CreateAllocations(VtxDataFacade);
		}

		// Write ValencyEntry hash for downstream nodes
		const FName EntryAttrName = PCGExValency::EntryData::GetEntryAttributeName(Settings->Suffix);
		ValencyEntryWriter = OutputFacade->GetWritable<int64>(EntryAttrName, 0, true, PCGExData::EBufferInit::Inherit);

		if (Settings->bOutputUnsolvableMarker)
		{
			UnsolvableWriter = OutputFacade->GetWritable<bool>(Settings->UnsolvableAttributeName, false, true, PCGExData::EBufferInit::Inherit);
		}

		// Get fixed pick reader and create filter cache if enabled
		if (Settings->bEnableFixedPicks)
		{
			FixedPickReader = VtxDataFacade->GetBroadcaster<FName>(Settings->FixedPickAttribute);

			// Create fixed pick filter cache
			FixedPickFilterCache = MakeShared<TArray<int8>>();
			FixedPickFilterCache->Init(Settings->bDefaultFixedPickFilterValue, VtxDataFacade->GetNum());

			// Register consumable attributes if we have filter factories
			if (FixedPickFilterFactories)
			{
				PCGExFactories::RegisterConsumableAttributesWithFacade(*FixedPickFilterFactories, VtxDataFacade);
			}
		}

		// Call base class AFTER creating writers (base triggers PrepareSingle)
		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		// Call base class first - forwards orbital readers to processor
		// (Orbital cache is built by processor in Process() after cluster is available)
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor)) { return false; }

		FProcessor* TypedProcessor = static_cast<FProcessor*>(InProcessor.Get());

		// Forward solver allocations to processor
		TypedProcessor->SolverAllocations = SolverAllocations;

		// Forward staging-specific writers to processor
		TypedProcessor->UnsolvableWriter = UnsolvableWriter;
		TypedProcessor->ValencyEntryWriter = ValencyEntryWriter;

		// Forward fixed pick reader, filter cache, and factories to processor
		TypedProcessor->FixedPickReader = FixedPickReader;
		TypedProcessor->FixedPickFilterCache = FixedPickFilterCache;
		TypedProcessor->FixedPickFilterFactories = FixedPickFilterFactories;

		return true;
	}

	void FBatch::CompleteWork()
	{
		VtxDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
