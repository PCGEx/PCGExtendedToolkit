// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExValencyBondingGenerative.h"

#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExActorCollection.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Graphs/PCGExGraph.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExSubGraph.h"
#include "Growth/PCGExValencyGrowthBFS.h"

#define LOCTEXT_NAMESPACE "PCGExValencyBondingGenerative"
#define PCGEX_NAMESPACE ValencyBondingGenerative

PCGEX_INITIALIZE_ELEMENT(ValencyBondingGenerative)
PCGEX_ELEMENT_BATCH_POINT_IMPL(ValencyBondingGenerative)

#pragma region UPCGExValencyBondingGenerativeSettings

void UPCGExValencyBondingGenerativeSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!GrowthStrategy) { GrowthStrategy = NewObject<UPCGExValencyGrowthBFSFactory>(this, TEXT("GrowthStrategy")); }
	}
	Super::PostInitProperties();
}

TArray<FPCGPinProperties> UPCGExValencyBondingGenerativeSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Edges of the generated growth trees", Required)
	PCGEX_PIN_PARAMS(PCGExValency::Labels::OutputValencyMapLabel, "Valency map for resolving ValencyEntry hashes", Required)
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExValencyBondingGenerativeContext

void FPCGExValencyBondingGenerativeContext::RegisterAssetDependencies()
{
	FPCGExPointsProcessorContext::RegisterAssetDependencies();

	const UPCGExValencyBondingGenerativeSettings* Settings = GetInputSettings<UPCGExValencyBondingGenerativeSettings>();
	if (!Settings) { return; }

	if (!Settings->BondingRules.IsNull())
	{
		AddAssetDependency(Settings->BondingRules.ToSoftObjectPath());
	}
}

#pragma endregion

#pragma region FPCGExValencyBondingGenerativeElement

bool FPCGExValencyBondingGenerativeElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyBondingGenerative)

	// Validate required settings
	if (Settings->BondingRules.IsNull())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No Bonding Rules provided."));
		return false;
	}

	PCGEX_OPERATION_VALIDATE(GrowthStrategy)

	// Load assets
	PCGExHelpers::LoadBlocking_AnyThreadTpl(Settings->BondingRules, InContext);

	return true;
}

void FPCGExValencyBondingGenerativeElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::PostLoadAssetsDependencies(InContext);

	PCGEX_CONTEXT_AND_SETTINGS(ValencyBondingGenerative)

	Context->BondingRules = Settings->BondingRules.Get();
}

bool FPCGExValencyBondingGenerativeElement::PostBoot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::PostBoot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(ValencyBondingGenerative)

	if (!Context->BondingRules)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to load Bonding Rules."));
		return false;
	}

	// Derive connector set from bonding rules
	Context->ConnectorSet = Context->BondingRules->ConnectorSet;
	if (!Context->ConnectorSet)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Bonding Rules has no Connector Set assigned."));
		return false;
	}

	// Ensure bonding rules are compiled
	if (!Context->BondingRules->IsCompiled())
	{
		if (!Context->BondingRules->Compile())
		{
			PCGE_LOG(Error, GraphAndLog, FTEXT("Failed to compile Bonding Rules."));
			return false;
		}
	}

	// Register growth factory
	Context->GrowthFactory = PCGEX_OPERATION_REGISTER_C(Context, UPCGExValencyGrowthFactory, Settings->GrowthStrategy, NAME_None);
	if (!Context->GrowthFactory) { return false; }

	// Create valency packer
	Context->ValencyPacker = MakeShared<PCGExValency::FValencyPacker>(Context);

	// Cache compiled rules
	Context->CompiledRules = Context->BondingRules->GetCompiledData();
	if (!Context->CompiledRules || Context->CompiledRules->ModuleCount == 0)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No compiled modules in bonding rules."));
		return false;
	}

	// Compile connector set
	Context->ConnectorSet->Compile();

	// Create edges IO collection for graph builder
	Context->EdgesIO = MakeShared<PCGExData::FPointIOCollection>(Context);

	// Build module local bounds cache from collection staging data
	const int32 ModuleCount = Context->CompiledRules->ModuleCount;
	Context->ModuleLocalBounds.SetNum(ModuleCount);
	for (int32 i = 0; i < ModuleCount; ++i)
	{
		Context->ModuleLocalBounds[i] = FBox(ForceInit);
	}

	// Populate bounds from mesh collection entries
	UPCGExMeshCollection* MeshCollection = Context->BondingRules->GetMeshCollection();
	if (MeshCollection)
	{
		MeshCollection->BuildCache();

		for (int32 ModuleIdx = 0; ModuleIdx < ModuleCount; ++ModuleIdx)
		{
			const int32 EntryIndex = Context->BondingRules->GetMeshEntryIndex(ModuleIdx);
			if (EntryIndex >= 0)
			{
				const FPCGExEntryAccessResult Result = MeshCollection->GetEntryRaw(EntryIndex);
				if (Result.IsValid())
				{
					Context->ModuleLocalBounds[ModuleIdx] = Result.Entry->Staging.Bounds;

					if (!FMath::IsNearlyZero(Settings->BoundsInflation))
					{
						Context->ModuleLocalBounds[ModuleIdx] = Context->ModuleLocalBounds[ModuleIdx].ExpandBy(Settings->BoundsInflation);
					}
				}
			}
		}
	}

	// Populate bounds from actor collection entries
	UPCGExActorCollection* ActorCollection = Context->BondingRules->GetActorCollection();
	if (ActorCollection)
	{
		ActorCollection->BuildCache();

		for (int32 ModuleIdx = 0; ModuleIdx < ModuleCount; ++ModuleIdx)
		{
			const int32 EntryIndex = Context->BondingRules->GetActorEntryIndex(ModuleIdx);
			if (EntryIndex >= 0)
			{
				const FPCGExEntryAccessResult Result = ActorCollection->GetEntryRaw(EntryIndex);
				if (Result.IsValid())
				{
					Context->ModuleLocalBounds[ModuleIdx] = Result.Entry->Staging.Bounds;

					if (!FMath::IsNearlyZero(Settings->BoundsInflation))
					{
						Context->ModuleLocalBounds[ModuleIdx] = Context->ModuleLocalBounds[ModuleIdx].ExpandBy(Settings->BoundsInflation);
					}
				}
			}
		}
	}

	// Build name-to-module lookup for seed filtering
	for (int32 ModuleIdx = 0; ModuleIdx < ModuleCount; ++ModuleIdx)
	{
		const FName& ModuleName = Context->CompiledRules->ModuleNames[ModuleIdx];
		if (!ModuleName.IsNone())
		{
			Context->NameToModules.FindOrAdd(ModuleName).Add(ModuleIdx);
		}
	}

	return true;
}

bool FPCGExValencyBondingGenerativeElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	PCGEX_CONTEXT_AND_SETTINGS(ValencyBondingGenerative)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("No seed points provided."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	// Output all processor-created IOs
	Context->MainPoints->StageOutputs();
	Context->MainBatch->Output();

	// Output valency map
	UPCGParamData* ValencyParamData = Context->ManagedObjects->New<UPCGParamData>();
	Context->ValencyPacker->PackToDataset(ValencyParamData);

	FPCGTaggedData& ValencyOutData = Context->OutputData.TaggedData.Emplace_GetRef();
	ValencyOutData.Pin = PCGExValency::Labels::OutputValencyMapLabel;
	ValencyOutData.Data = ValencyParamData;

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExValencyBondingGenerative::FProcessor

namespace PCGExValencyBondingGenerative
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExValencyBondingGenerative::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		const int32 NumSeeds = PointDataFacade->GetNum();
		if (NumSeeds == 0) { return false; }

		// Allocate resolved module array
		ResolvedModules.SetNumUninitialized(NumSeeds);
		for (int32 i = 0; i < NumSeeds; ++i) { ResolvedModules[i] = -1; }

		// Prepare name attribute reader for seed filtering
		if (!Settings->SeedModuleNameAttribute.IsNone())
		{
			NameReader = PointDataFacade->GetReadable<FName>(Settings->SeedModuleNameAttribute);
		}

		// Create growth operation
		GrowthOp = Context->GrowthFactory->CreateOperation();
		if (!GrowthOp) { return false; }

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExValencyBondingGenerative::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		const FPCGExValencyBondingRulesCompiled* CompiledRules = Context->CompiledRules;
		const TMap<FName, TArray<int32>>& NameToModules = Context->NameToModules;

		PCGEX_SCOPE_LOOP(Index)
		{
			// Determine candidate modules for this seed
			TArray<int32> CandidateModules;

			if (NameReader)
			{
				const FName RequestedName = NameReader->Read(Index);
				if (!RequestedName.IsNone())
				{
					if (const TArray<int32>* Matching = NameToModules.Find(RequestedName))
					{
						CandidateModules = *Matching;
					}
				}
			}

			// If no filtering or no match, use all modules that have connectors
			if (CandidateModules.IsEmpty())
			{
				for (int32 ModuleIdx = 0; ModuleIdx < CompiledRules->ModuleCount; ++ModuleIdx)
				{
					if (CompiledRules->GetModuleConnectorCount(ModuleIdx) > 0)
					{
						CandidateModules.Add(ModuleIdx);
					}
				}
			}

			if (CandidateModules.IsEmpty())
			{
				ResolvedModules[Index] = -1;
				continue;
			}

			// Weighted random selection using per-point deterministic seed
			float TotalWeight = 0.0f;
			for (const int32 ModuleIdx : CandidateModules)
			{
				TotalWeight += CompiledRules->ModuleWeights[ModuleIdx];
			}

			int32 SelectedModule = CandidateModules[0];
			if (TotalWeight > 0.0f)
			{
				FRandomStream PointRandom(HashCombine(Settings->Seed, Index));
				float Pick = PointRandom.FRand() * TotalWeight;
				for (const int32 ModuleIdx : CandidateModules)
				{
					Pick -= CompiledRules->ModuleWeights[ModuleIdx];
					if (Pick <= 0.0f)
					{
						SelectedModule = ModuleIdx;
						break;
					}
				}
			}

			ResolvedModules[Index] = SelectedModule;
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		const FPCGExValencyBondingRulesCompiled* CompiledRules = Context->CompiledRules;

		// Setup budget and bounds tracker for this dataset
		FPCGExGrowthBudget Budget = Settings->Budget;
		Budget.Reset();

		FPCGExBoundsTracker BoundsTracker;

		// Initialize growth operation with per-dataset state
		GrowthOp->Initialize(CompiledRules, Context->ConnectorSet, BoundsTracker, Budget, Settings->Seed);
		GrowthOp->ModuleLocalBounds = Context->ModuleLocalBounds;

		// Build placed module entries from resolved seeds
		const int32 NumSeeds = PointDataFacade->GetNum();
		TConstPCGValueRange<FTransform> SeedTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();

		int32 SeedIdx = 0;
		for (int32 Index = 0; Index < NumSeeds; ++Index)
		{
			const int32 SelectedModule = ResolvedModules[Index];
			if (SelectedModule < 0) { continue; }

			FPCGExPlacedModule SeedModule;
			SeedModule.ModuleIndex = SelectedModule;
			SeedModule.WorldTransform = SeedTransforms[Index];
			SeedModule.WorldBounds = GrowthOp->ComputeWorldBounds(SelectedModule, SeedModule.WorldTransform);
			SeedModule.ParentIndex = -1;
			SeedModule.ParentConnectorIndex = -1;
			SeedModule.ChildConnectorIndex = -1;
			SeedModule.Depth = 0;
			SeedModule.SeedIndex = SeedIdx;
			SeedModule.CumulativeWeight = CompiledRules->ModuleWeights[SelectedModule];

			PlacedModules.Add(SeedModule);
			Budget.CurrentTotal++;

			if (SeedModule.WorldBounds.IsValid)
			{
				BoundsTracker.Add(SeedModule.WorldBounds);
			}

			SeedIdx++;
		}

		if (PlacedModules.IsEmpty()) { return; }

		// Run the growth (sequential)
		GrowthOp->Grow(PlacedModules);

		// Create output point data
		if (!PointDataFacade->Source->InitializeOutput(PCGExData::EIOInit::New))
		{
			bIsProcessorValid = false;
			return;
		}
		
		TSharedPtr<PCGExData::FPointIO> OutputIO = PointDataFacade->Source;
		UPCGBasePointData* OutPointData = OutputIO->GetOut();

		const int32 TotalPlaced = PlacedModules.Num();

		// Allocate points with transform + bounds
		const EPCGPointNativeProperties AllocatedProperties = EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax | EPCGPointNativeProperties::Seed;
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPointData, TotalPlaced, AllocatedProperties);

		// Get write ranges
		TPCGValueRange<FTransform> OutTransforms = OutPointData->GetTransformValueRange(false);
		TPCGValueRange<FVector> OutBoundsMin = OutPointData->GetBoundsMinValueRange(false);
		TPCGValueRange<FVector> OutBoundsMax = OutPointData->GetBoundsMaxValueRange(false);
		TPCGValueRange<int32> OutSeeds = OutPointData->GetSeedValueRange(false);

		// Create ValencyEntry writer for Valency Map pipeline
		const FName ValencyEntryAttrName = PCGExValency::EntryData::GetEntryAttributeName(Settings->EntrySuffix);
		TSharedPtr<PCGExData::TBuffer<int64>> ValencyEntryWriter = PointDataFacade->GetWritable<int64>(ValencyEntryAttrName, 0, true, PCGExData::EBufferInit::Inherit);

		TSharedPtr<PCGExData::TBuffer<FName>> ModuleNameWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> DepthWriter;
		TSharedPtr<PCGExData::TBuffer<int32>> SeedIndexWriter;

		if (Settings->bOutputModuleName)
		{
			ModuleNameWriter = PointDataFacade->GetWritable<FName>(Settings->ModuleNameAttributeName, NAME_None, true, PCGExData::EBufferInit::Inherit);
		}

		if (Settings->bOutputDepth)
		{
			DepthWriter = PointDataFacade->GetWritable<int32>(Settings->DepthAttributeName, 0, true, PCGExData::EBufferInit::Inherit);
		}

		if (Settings->bOutputSeedIndex)
		{
			SeedIndexWriter = PointDataFacade->GetWritable<int32>(Settings->SeedIndexAttributeName, 0, true, PCGExData::EBufferInit::Inherit);
		}

		// Write vertex orbital masks if orbital data output is enabled
		TSharedPtr<PCGExData::TBuffer<int64>> OrbitalMaskWriter;
		if (Settings->bOutputOrbitalData)
		{
			const FName MaskAttrName = PCGExValency::Attributes::GetMaskAttributeName(Settings->EntrySuffix);
			OrbitalMaskWriter = PointDataFacade->GetWritable<int64>(MaskAttrName, 0, true, PCGExData::EBufferInit::Inherit);
		}

		// Build per-vertex orbital masks from connectivity
		TArray<int64> VertexOrbitalMasks;
		if (Settings->bOutputOrbitalData)
		{
			VertexOrbitalMasks.SetNumZeroed(TotalPlaced);

			for (int32 i = 0; i < TotalPlaced; ++i)
			{
				const FPCGExPlacedModule& Placed = PlacedModules[i];
				if (Placed.ParentIndex < 0) { continue; }

				// Child connector's orbital -> bit on this vertex
				const FIntPoint& ChildHeader = CompiledRules->ModuleConnectorHeaders[Placed.ModuleIndex];
				const int32 ChildConnIdx = ChildHeader.X + Placed.ChildConnectorIndex;
				if (CompiledRules->AllModuleConnectors.IsValidIndex(ChildConnIdx))
				{
					const int32 ChildOrbital = CompiledRules->AllModuleConnectors[ChildConnIdx].OrbitalIndex;
					if (ChildOrbital >= 0 && ChildOrbital < 64)
					{
						VertexOrbitalMasks[i] |= (1LL << ChildOrbital);
					}
				}

				// Parent connector's orbital -> bit on parent vertex
				const FIntPoint& ParentHeader = CompiledRules->ModuleConnectorHeaders[PlacedModules[Placed.ParentIndex].ModuleIndex];
				const int32 ParentConnIdx = ParentHeader.X + Placed.ParentConnectorIndex;
				if (CompiledRules->AllModuleConnectors.IsValidIndex(ParentConnIdx))
				{
					const int32 ParentOrbital = CompiledRules->AllModuleConnectors[ParentConnIdx].OrbitalIndex;
					if (ParentOrbital >= 0 && ParentOrbital < 64)
					{
						VertexOrbitalMasks[Placed.ParentIndex] |= (1LL << ParentOrbital);
					}
				}
			}
		}

		// Write output vertex data
		for (int32 PlacedIdx = 0; PlacedIdx < TotalPlaced; ++PlacedIdx)
		{
			const FPCGExPlacedModule& Placed = PlacedModules[PlacedIdx];
			const int32 ModuleIdx = Placed.ModuleIndex;

			// Transform
			FTransform& OutTransform = OutTransforms[PlacedIdx];
			OutTransform = Placed.WorldTransform;

			// Seed for deterministic downstream use
			OutSeeds[PlacedIdx] = HashCombine(Settings->Seed, PlacedIdx);

			// Apply local transform if enabled
			if (Settings->bApplyLocalTransforms && CompiledRules->ModuleHasLocalTransform[ModuleIdx])
			{
				const FTransform LocalTransform = CompiledRules->GetModuleLocalTransform(ModuleIdx, OutSeeds[PlacedIdx]);
				OutTransform = LocalTransform * OutTransform;
			}

			// Set default bounds
			const FBox& ModuleBounds = Context->ModuleLocalBounds[ModuleIdx];
			if (ModuleBounds.IsValid)
			{
				OutBoundsMin[PlacedIdx] = ModuleBounds.Min;
				OutBoundsMax[PlacedIdx] = ModuleBounds.Max;
			}
			else
			{
				OutBoundsMin[PlacedIdx] = -FVector::OneVector;
				OutBoundsMax[PlacedIdx] = FVector::OneVector;
			}

			// Write ValencyEntry hash for Valency Map pipeline
			if (ValencyEntryWriter && Context->ValencyPacker)
			{
				const uint64 ValencyHash = Context->ValencyPacker->GetEntryIdx(
					Context->BondingRules, static_cast<uint16>(ModuleIdx));
				ValencyEntryWriter->SetValue(PlacedIdx, static_cast<int64>(ValencyHash));
			}

			// Write vertex orbital mask
			if (OrbitalMaskWriter && Settings->bOutputOrbitalData)
			{
				OrbitalMaskWriter->SetValue(PlacedIdx, VertexOrbitalMasks[PlacedIdx]);
			}

			// Write module name
			if (ModuleNameWriter)
			{
				ModuleNameWriter->SetValue(PlacedIdx, CompiledRules->ModuleNames[ModuleIdx]);
			}

			// Write depth
			if (DepthWriter)
			{
				DepthWriter->SetValue(PlacedIdx, Placed.Depth);
			}

			// Write seed index
			if (SeedIndexWriter)
			{
				SeedIndexWriter->SetValue(PlacedIdx, Placed.SeedIndex);
			}
		}

		// ==================== Graph Creation ====================

		// Build edges from parent-child connectivity
		TSet<uint64> UniqueEdges;
		// Also build mapping: H64U(ParentIdx, ChildIdx) -> ChildPlacedIdx for edge attribute lookup
		TMap<uint64, int32> EdgeToChildIndex;

		for (int32 i = 0; i < TotalPlaced; ++i)
		{
			if (PlacedModules[i].ParentIndex >= 0)
			{
				const uint64 EdgeHash = PCGEx::H64U(PlacedModules[i].ParentIndex, i);
				UniqueEdges.Add(EdgeHash);
				EdgeToChildIndex.Add(EdgeHash, i);
			}
		}

		if (UniqueEdges.IsEmpty()) { return; }

		// Create graph builder
		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);
		GraphBuilder->bInheritNodeData = false;
		GraphBuilder->EdgesIO = Context->EdgesIO;
		GraphBuilder->NodePointsTransforms = OutPointData->GetConstTransformValueRange();

		// Create graph and insert edges
		GraphBuilder->Graph = MakeShared<PCGExGraphs::FGraph>(TotalPlaced);
		GraphBuilder->Graph->InsertEdges(UniqueEdges, BatchIndex);

		// Capture data for edge attribute writing in post-compile callback
		const bool bWriteOrbital = Settings->bOutputOrbitalData;
		const bool bWriteConnector = Settings->bOutputConnectorData;
		const FName OrbitalSuffix = Settings->EntrySuffix;
		const UPCGExValencyConnectorSet* ConnectorSetPtr = Context->ConnectorSet;

		if (bWriteOrbital || bWriteConnector)
		{
			// Capture PlacedModules and CompiledRules for the callback
			TArray<FPCGExPlacedModule> CapturedPlacedModules = PlacedModules;
			const FPCGExValencyBondingRulesCompiled* CapturedCompiledRules = CompiledRules;
			TMap<uint64, int32> CapturedEdgeToChildIndex = MoveTemp(EdgeToChildIndex);

			GraphBuilder->OnSubGraphPostProcess = [bWriteOrbital, bWriteConnector, OrbitalSuffix, ConnectorSetPtr,
				CapturedPlacedModules = MoveTemp(CapturedPlacedModules),
				CapturedCompiledRules,
				CapturedEdgeToChildIndex = MoveTemp(CapturedEdgeToChildIndex)](const TSharedRef<PCGExGraphs::FSubGraph>& SubGraph)
			{
				const TSharedPtr<PCGExData::FFacade>& EdgeFacade = SubGraph->EdgesDataFacade;

				const int32 NumEdges = SubGraph->FlattenedEdges.Num();

				// Create edge attribute writers
				TSharedPtr<PCGExData::TBuffer<int64>> OrbitalWriter;
				TSharedPtr<PCGExData::TBuffer<int64>> ConnectorWriter;

				if (bWriteOrbital)
				{
					const FName OrbitalAttrName = PCGExValency::Attributes::GetOrbitalAttributeName(OrbitalSuffix);
					OrbitalWriter = EdgeFacade->GetWritable<int64>(OrbitalAttrName, 0, true, PCGExData::EBufferInit::Inherit);
				}

				if (bWriteConnector && ConnectorSetPtr)
				{
					const FName ConnectorAttrName = ConnectorSetPtr->GetConnectorAttributeName();
					ConnectorWriter = EdgeFacade->GetWritable<int64>(ConnectorAttrName, 0, true, PCGExData::EBufferInit::Inherit);
				}

				// Write edge attributes
				for (int32 EdgeIdx = 0; EdgeIdx < NumEdges; ++EdgeIdx)
				{
					const PCGExGraphs::FEdge& Edge = SubGraph->FlattenedEdges[EdgeIdx];
					if (!Edge.bValid) { continue; }

					// Find which placed module this edge corresponds to
					const uint64 EdgeHash = PCGEx::H64U(Edge.Start, Edge.End);
					const int32* ChildIdxPtr = CapturedEdgeToChildIndex.Find(EdgeHash);
					if (!ChildIdxPtr) { continue; }

					const FPCGExPlacedModule& Child = CapturedPlacedModules[*ChildIdxPtr];
					const int32 ParentIdx = Child.ParentIndex;
					if (ParentIdx < 0) { continue; }

					const FPCGExPlacedModule& Parent = CapturedPlacedModules[ParentIdx];

					if (OrbitalWriter)
					{
						// Get orbital indices for both endpoints
						int32 ParentOrbital = -1;
						int32 ChildOrbital = -1;

						const FIntPoint& ParentHeader = CapturedCompiledRules->ModuleConnectorHeaders[Parent.ModuleIndex];
						const int32 ParentConnFlatIdx = ParentHeader.X + Child.ParentConnectorIndex;
						if (CapturedCompiledRules->AllModuleConnectors.IsValidIndex(ParentConnFlatIdx))
						{
							ParentOrbital = CapturedCompiledRules->AllModuleConnectors[ParentConnFlatIdx].OrbitalIndex;
						}

						const FIntPoint& ChildHeader = CapturedCompiledRules->ModuleConnectorHeaders[Child.ModuleIndex];
						const int32 ChildConnFlatIdx = ChildHeader.X + Child.ChildConnectorIndex;
						if (CapturedCompiledRules->AllModuleConnectors.IsValidIndex(ChildConnFlatIdx))
						{
							ChildOrbital = CapturedCompiledRules->AllModuleConnectors[ChildConnFlatIdx].OrbitalIndex;
						}

						const uint8 ParentOrbU8 = static_cast<uint8>(FMath::Max(0, ParentOrbital));
						const uint8 ChildOrbU8 = static_cast<uint8>(FMath::Max(0, ChildOrbital));

						// Pack: determine which endpoint is Start vs End
						// FEdge.Start/End are point indices. Parent is Start if Edge.Start matches ParentIdx
						const int64 PackedOrbital = (static_cast<int32>(Edge.Start) == ParentIdx)
							? PCGExValency::EdgeOrbital::Pack(ParentOrbU8, ChildOrbU8)
							: PCGExValency::EdgeOrbital::Pack(ChildOrbU8, ParentOrbU8);

						OrbitalWriter->SetValue(EdgeIdx, PackedOrbital);
					}

					if (ConnectorWriter)
					{
						// Pack connector instance indices: source = start endpoint, target = end endpoint
						const int64 PackedConnector = (static_cast<int32>(Edge.Start) == ParentIdx)
							? PCGExValency::EdgeConnector::Pack(Child.ParentConnectorIndex, Child.ChildConnectorIndex)
							: PCGExValency::EdgeConnector::Pack(Child.ChildConnectorIndex, Child.ParentConnectorIndex);

						ConnectorWriter->SetValue(EdgeIdx, PackedConnector);
					}
				}

			};
		}

		// Compile graph asynchronously
		GraphBuilder->CompileAsync(TaskManager, true, nullptr);
	}

	void FProcessor::Output()
	{
		if (GraphBuilder && GraphBuilder->bCompiledSuccessfully)
		{
			GraphBuilder->StageEdgesOutputs();
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
