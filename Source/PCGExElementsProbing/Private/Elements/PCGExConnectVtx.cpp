// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExConnectVtx.h"

#include "PCGExH.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbingEngine.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Factories/PCGExFactories.h"
#include "Graphs/PCGExGraphPatcher.h"

#define LOCTEXT_NAMESPACE "PCGExConnectVtx"
#define PCGEX_NAMESPACE ConnectVtx

PCGExData::EIOInit UPCGExConnectVtxSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExConnectVtxSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

TArray<FPCGPinProperties> UPCGExConnectVtxSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExClusters::Labels::SourceProbesLabel, "Probes used to connect vtx", Required, FPCGExDataTypeInfoProbe::AsId())

	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceFilterGenerators, "Vtx that don't meet requirements won't generate connections. Supports both point & vtx filters.", Normal)
	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceFilterConnectables, "Vtx that don't meet requirements can't receive connections. Supports both point & vtx filters.", Normal)

	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(ConnectVtx)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ConnectVtx)

FPCGExConnectVtxContext::~FPCGExConnectVtxContext()
{
}

#pragma region UPCGExConnectVtxSettings / FPCGExConnectVtxElement

bool FPCGExConnectVtxElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ConnectVtx)

	if (!PCGExFactories::GetInputFactories<UPCGExProbeFactoryData>(Context, PCGExClusters::Labels::SourceProbesLabel, Context->ProbeFactories, {FPCGExDataTypeInfoProbe::AsId()}))
	{
		return false;
	}

	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceFilterGenerators, Context->GeneratorsFiltersFactories, PCGExFactories::ClusterNodeFilters(), false);
	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceFilterConnectables, Context->ConnectablesFiltersFactories, PCGExFactories::ClusterNodeFilters(), false);

	Context->CWCoincidenceTolerance = FVector(Settings->CoincidenceTolerance);

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	PCGEX_FWD(VtxCarryOverDetails)
	Context->VtxCarryOverDetails.Init();

	// The relation dropdown is only meaningful (and visible) under Vtx Group scope: Cluster scope forces
	// same-cluster and hides it, All scope satisfies every relation. Reject the one unsatisfiable case -
	// Cross-Data while the property is visible (Vtx Group) - and ignore any stale value otherwise.
	if (Settings->EdgeRelation == EPCGExConnectVtxEdgeRelation::CrossData && Settings->Scope == EPCGExConnectVtxScope::VtxGroup)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Cross-Data edge relation requires the All Inputs scope."));
		return false;
	}

	if (Settings->bFlagVtxConnector)
	{
		PCGEX_VALIDATE_NAME(Settings->VtxConnectorFlagName)
	}
	if (Settings->bFlagEdgeConnector)
	{
		PCGEX_VALIDATE_NAME(Settings->EdgeConnectorFlagName)
	}

	return true;
}

bool FPCGExConnectVtxElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExConnectVtxElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ConnectVtx)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		const bool bWantsWriteStep = Settings->Scope != EPCGExConnectVtxScope::All;
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [bWantsWriteStep](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = bWantsWriteStep;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	if (Settings->Scope == EPCGExConnectVtxScope::All)
	{
		PCGEX_CLUSTER_BATCH_PROCESSING(PCGExConnectVtx::State_LaunchingMerge)

		PCGEX_ON_STATE(PCGExConnectVtx::State_LaunchingMerge)
		{
			if (!LaunchVtxMerge(Context, Settings)) { return true; }
		}

		PCGEX_ON_ASYNC_STATE_READY(PCGExConnectVtx::State_MergingVtx)
		{
			if (!LaunchProbing(Context, Settings)) { return true; }
		}

		PCGEX_ON_ASYNC_STATE_READY(PCGExConnectVtx::State_Probing)
		{
			if (!LaunchPatching(Context, Settings)) { return true; }
		}

		PCGEX_ON_ASYNC_STATE_READY(PCGExConnectVtx::State_Patching)
		{
			if (!CommitAndOutput(Context, Settings)) { return true; }
		}

		return Context->TryComplete();
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

#pragma endregion

#pragma region All-Inputs pipeline (context-level)

bool FPCGExConnectVtxElement::LaunchVtxMerge(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const
{
	using namespace PCGExConnectVtx;

	Context->VtxMerger = MakeShared<PCGExGraphs::FVtxMerger>();

	int32 ClusterIdOffset = 0;

	for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Context->Batches)
	{
		const TSharedPtr<FBatch> TypedBatch = StaticCastSharedPtr<FBatch>(Batch);

		// Batches with no usable clusters already forwarded themselves (FBatch::CompleteWork).
		if (!TypedBatch->bIsBatchValid || TypedBatch->ValidClusters.IsEmpty())
		{
			continue;
		}

		TypedBatch->GroupIdOffset = ClusterIdOffset;
		ClusterIdOffset += TypedBatch->ValidClusters.Num();

		Context->VtxMerger->AddSource(TypedBatch->VtxDataFacade->Source);
		Context->ValidBatches.Add(TypedBatch);
	}

	if (Context->ValidBatches.IsEmpty())
	{
		if (!Settings->bQuietNoConnectionWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No valid clusters to connect."));
		}
		Context->OutputPointsAndEdges();
		Context->Done();
		return true;
	}

	Context->VtxMerger->MergeAsync(Context, Context->GetTaskManager(), &Context->VtxCarryOverDetails);
	Context->SetState(State_MergingVtx);

	return true;
}

bool FPCGExConnectVtxElement::LaunchProbing(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const
{
	using namespace PCGExConnectVtx;

	Context->MergedFacade = Context->VtxMerger->Finalize(Context, PCGExClusters::Labels::OutputVerticesLabel);
	if (!Context->MergedFacade)
	{
		Context->CancelExecution(TEXT("Vtx merge failed."));
		return false;
	}

	Context->Engine = MakeShared<PCGExProbing::FProbingEngine>(Context->MergedFacade.ToSharedRef());
	Context->Engine->SetCoincidence(Settings->bPreventCoincidence, Context->CWCoincidenceTolerance);
	if (Settings->bProjectPoints)
	{
		Context->Engine->SetProjection(Settings->ProjectionDetails);
	}

	if (!Context->Engine->Init(Context, Context->ProbeFactories))
	{
		Context->CancelExecution(TEXT("No probe could be initialized."));
		return false;
	}

	// Assemble the merged-domain masks, group ids & existing-edge hashes from the per-batch data.
	const bool bWantsGroupIds = Settings->EdgeRelation != EPCGExConnectVtxEdgeRelation::Any;
	if (bWantsGroupIds)
	{
		Context->GroupIds.Init(INDEX_NONE, Context->MergedFacade->GetNum());
	}

	for (int32 b = 0; b < Context->ValidBatches.Num(); ++b)
	{
		const TSharedPtr<FBatch>& Batch = Context->ValidBatches[b];

		// Offsets are authoritative only after MergeAsync; ValidBatches[b] is source b by construction.
		Batch->MergeOffset = Context->VtxMerger->GetOffset(b);
		const int32 Offset = Batch->MergeOffset;
		const int32 Num = Batch->VtxDataFacade->GetNum();

		FMemory::Memcpy(Context->Engine->CanGenerate.GetData() + Offset, Batch->CanGenerateCache->GetData(), Num);
		FMemory::Memcpy(Context->Engine->AcceptConnections.GetData() + Offset, Batch->AcceptConnectionsCache->GetData(), Num);

		if (Settings->EdgeRelation == EPCGExConnectVtxEdgeRelation::CrossCluster)
		{
			for (int32 i = 0; i < Num; ++i)
			{
				const int32 ClusterId = Batch->ClusterIds[i];
				Context->GroupIds[Offset + i] = ClusterId == INDEX_NONE ? INDEX_NONE : Batch->GroupIdOffset + ClusterId;
			}
		}
		else if (Settings->EdgeRelation == EPCGExConnectVtxEdgeRelation::CrossData)
		{
			for (int32 i = 0; i < Num; ++i)
			{
				Context->GroupIds[Offset + i] = b;
			}
		}

		Context->ExistingEdges.Reserve(Context->ExistingEdges.Num() + Batch->ExistingEdges.Num());
		for (const uint64 E : Batch->ExistingEdges)
		{
			uint32 A;
			uint32 B;
			PCGEx::H64(E, A, B);
			Context->ExistingEdges.Add(PCGEx::H64U(A + Offset, B + Offset));
		}
	}

	if (bWantsGroupIds)
	{
		Context->Engine->PointGroupIds = &Context->GroupIds;
		Context->Engine->EdgeRelation = PCGExProbing::EEdgeRelation::DifferentGroup;
	}

	// The engine owns the whole schedule; it collapses the scoped edges before the state machine
	// advances (poll on IsWaitingForTasks), so LaunchPatching sees a finished GetUniqueEdges().
	Context->Engine->RunAsync(Context->GetTaskManager(), []() {});
	Context->SetState(State_Probing);

	return true;
}

bool FPCGExConnectVtxElement::LaunchPatching(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const
{
	using namespace PCGExConnectVtx;

	Context->Patcher = MakeShared<PCGExGraphs::FGraphPatcher>(Context->MergedFacade.ToSharedRef());

	for (const TSharedPtr<FBatch>& Batch : Context->ValidBatches)
	{
		const int32 SourceIndex = Context->Patcher->AddVtxSource(Batch->VtxDataFacade->Source, Batch->MergeOffset);
		RegisterClusterGroups(*Context->Patcher, Batch->ValidClusters, Batch->MergeOffset, SourceIndex);
	}

	StageProbeEdges(*Context->Patcher, Context->Engine->GetUniqueEdges(), Context->ExistingEdges, Context->ConnectorEdgeHandles, Context->ConnectorEndpoints);

	if (Context->ConnectorEdgeHandles.IsEmpty() && !Settings->bQuietNoConnectionWarning)
	{
		PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("No connection was created."));
	}

	Context->Patcher->ResolveAndMergeAsync(Context->MainEdges.ToSharedRef(), Context->GetTaskManager(), &Context->CarryOverDetails);
	Context->SetState(State_Patching);

	return true;
}

bool FPCGExConnectVtxElement::CommitAndOutput(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const
{
	Context->Patcher->Commit();

	PCGExGraphs::WriteConnectorFlags(
		*Context->Patcher, Context->MergedFacade.ToSharedRef(),
		{Settings->bFlagVtxConnector, Settings->VtxConnectorFlagName, Settings->bFlagEdgeConnector, Settings->EdgeConnectorFlagName},
		Context->ConnectorEdgeHandles, Context->ConnectorEndpoints);

	(void)Context->MergedFacade->Source->StageOutput(Context);
	Context->OutputPointsAndEdges();
	Context->Done();

	return true;
}

#pragma endregion

namespace PCGExConnectVtx
{
#pragma region Shared staging helpers

	void RegisterClusterGroups(PCGExGraphs::FGraphPatcher& InPatcher, const TArray<TSharedPtr<PCGExClusters::FCluster>>& InClusters, const int32 InOffset, const int32 InSourceIndex)
	{
		for (const TSharedPtr<PCGExClusters::FCluster>& Cl : InClusters)
		{
			TArray<int32> VtxIndices;
			VtxIndices.Reserve(Cl->Nodes->Num());
			for (const PCGExClusters::FNode& Node : *Cl->Nodes)
			{
				VtxIndices.Add(InOffset + Node.PointIndex);
			}
			InPatcher.AddEdgeGroup(Cl->EdgesIO.Pin(), VtxIndices, InSourceIndex);
		}
	}

	void StageProbeEdges(PCGExGraphs::FGraphPatcher& InPatcher, const TSet<uint64>& InNewEdges, const TSet<uint64>& InExistingEdges, TArray<int32>& OutHandles, TArray<uint64>& OutEndpoints)
	{
		OutHandles.Reserve(InNewEdges.Num());
		OutEndpoints.Reserve(InNewEdges.Num());

		for (const uint64 E : InNewEdges)
		{
			if (InExistingEdges.Contains(E))
			{
				continue;
			}

			uint32 A;
			uint32 B;
			PCGEx::H64(E, A, B);
			if (A == B)
			{
				continue;
			}

			OutHandles.Add(InPatcher.AddEdge(A, B));
			OutEndpoints.Add(E);
		}
	}

#pragma endregion

#pragma region FProcessor

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExConnectVtx::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Filter managers are per-cluster (bound to this processor's Cluster + edge facade). Building them
		// here is parallel-safe (no shared writes); mask evaluation is deferred to the sequential
		// FBatch::CompleteWork pass so shared-vtx results are deterministic.
		if (!Context->GeneratorsFiltersFactories.IsEmpty())
		{
			GeneratorsFilter = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
			GeneratorsFilter->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
			if (!GeneratorsFilter->Init(ExecutionContext, Context->GeneratorsFiltersFactories))
			{
				return false;
			}
		}

		if (!Context->ConnectablesFiltersFactories.IsEmpty())
		{
			ConnectablesFilter = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
			ConnectablesFilter->SetSupportedTypes(&PCGExFactories::ClusterNodeFilters());
			if (!ConnectablesFilter->Init(ExecutionContext, Context->ConnectablesFiltersFactories))
			{
				return false;
			}
		}

		return true;
	}

	void FProcessor::ContributeMasks(const TSharedPtr<TArray<int8>>& InCanGenerate, const TSharedPtr<TArray<int8>>& InAcceptConnections) const
	{
		const TArrayView<PCGExClusters::FNode> NodesView = MakeArrayView(*Cluster->Nodes);

		if (GeneratorsFilter)
		{
			GeneratorsFilter->Test(NodesView, InCanGenerate, false);
		}
		else
		{
			TArray<int8>& CanGenerate = *InCanGenerate;
			for (const PCGExClusters::FNode& Node : NodesView)
			{
				CanGenerate[Node.PointIndex] = 1;
			}
		}

		if (ConnectablesFilter)
		{
			ConnectablesFilter->Test(NodesView, InAcceptConnections, false);
		}
		else
		{
			TArray<int8>& AcceptConnections = *InAcceptConnections;
			for (const PCGExClusters::FNode& Node : NodesView)
			{
				AcceptConnections[Node.PointIndex] = 1;
			}
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExConnectVtxContext, UPCGExConnectVtxSettings>::Cleanup();
		GeneratorsFilter.Reset();
		ConnectablesFilter.Reset();
	}

#pragma endregion

#pragma region FBatch

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectVtx)

		// Per-batch scopes patch the vtx in place; All scope merges into a fresh dataset instead.
		if (Settings->Scope != EPCGExConnectVtxScope::All)
		{
			InVtx->InitializeOutput(PCGExData::EIOInit::Duplicate);
		}
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectVtx)

		PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->GeneratorsFiltersFactories, FacadePreloader);
		PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->ConnectablesFiltersFactories, FacadePreloader);
	}

	void FBatch::Process()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectVtx)

		// Flag the filter-input attributes consumable so they're cleaned from the mutable vtx output
		// (the stock IBatch vtx-filter path does this; our per-node filter pins bypass it).
		PCGExFactories::RegisterConsumableAttributesWithFacade(Context->GeneratorsFiltersFactories, VtxDataFacade);
		PCGExFactories::RegisterConsumableAttributesWithFacade(Context->ConnectablesFiltersFactories, VtxDataFacade);

		const int32 NumVtx = VtxDataFacade->GetNum();

		CanGenerateCache = MakeShared<TArray<int8>>();
		CanGenerateCache->Init(0, NumVtx);

		AcceptConnectionsCache = MakeShared<TArray<int8>>();
		AcceptConnectionsCache->Init(0, NumVtx);

		TBatch<FProcessor>::Process();
	}

	void FBatch::ForwardUntouched()
	{
		VtxDataFacade->Source->InitializeOutput(PCGExData::EIOInit::Forward);
		for (const TSharedPtr<PCGExData::FPointIO>& EdgesIO : Edges)
		{
			EdgesIO->InitializeOutput(PCGExData::EIOInit::Forward);
		}
	}

	void FBatch::BuildTopologyAndMasks()
	{
		const int32 NumVtx = VtxDataFacade->GetNum();
		ClusterIds.Init(INDEX_NONE, NumVtx);

		for (int32 ClusterIndex = 0; ClusterIndex < ValidClusters.Num(); ++ClusterIndex)
		{
			const TSharedPtr<PCGExClusters::FCluster>& Cl = ValidClusters[ClusterIndex];

			for (const PCGExClusters::FNode& Node : *Cl->Nodes)
			{
				ClusterIds[Node.PointIndex] = ClusterIndex;
			}

			ExistingEdges.Reserve(ExistingEdges.Num() + Cl->Edges->Num());
			for (const PCGExGraphs::FEdge& E : *Cl->Edges)
			{
				ExistingEdges.Add(PCGEx::H64U(E.Start, E.End));
			}
		}

		// Deterministic sequential fill: processors run in a stable order, so a vtx shared by two clusters
		// (degenerate input) resolves to a fixed last-writer instead of a data race on the shared caches.
		for (const TSharedRef<PCGExClusterMT::IProcessor>& P : Processors)
		{
			if (!P->Cluster)
			{
				continue;
			}
			StaticCastSharedRef<FProcessor>(P)->ContributeMasks(CanGenerateCache, AcceptConnectionsCache);
		}
	}

	void FBatch::CompleteWork()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectVtx)

		const int32 NumValidClusters = GatherValidClusters();

		if (Processors.Num() != NumValidClusters)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some vtx/edges groups have invalid clusters. Make sure to sanitize the input first."));
		}

		if (ValidClusters.IsEmpty())
		{
			// Nothing to connect: pass the group through untouched (both scopes).
			ForwardUntouched();
			return;
		}

		BuildTopologyAndMasks();

		if (Settings->Scope == EPCGExConnectVtxScope::All)
		{
			// The context-level pipeline takes over once every batch is done.
			return;
		}

		Engine = MakeShared<PCGExProbing::FProbingEngine>(VtxDataFacade);
		Engine->SetCoincidence(Settings->bPreventCoincidence, Context->CWCoincidenceTolerance);
		if (Settings->bProjectPoints)
		{
			Engine->SetProjection(Settings->ProjectionDetails);
		}

		if (!Engine->Init(ExecutionContext, Context->ProbeFactories))
		{
			// No probe can produce edges: pass the group through untouched.
			Engine.Reset();
			ForwardUntouched();
			return;
		}

		Engine->CanGenerate = MoveTemp(*CanGenerateCache);
		Engine->AcceptConnections = MoveTemp(*AcceptConnectionsCache);

		if (Settings->Scope == EPCGExConnectVtxScope::Cluster)
		{
			Engine->PointGroupIds = &ClusterIds;
			Engine->EdgeRelation = PCGExProbing::EEdgeRelation::SameGroup;
		}
		else if (Settings->EdgeRelation == EPCGExConnectVtxEdgeRelation::CrossCluster)
		{
			Engine->PointGroupIds = &ClusterIds;
			Engine->EdgeRelation = PCGExProbing::EEdgeRelation::DifferentGroup;
		}

		Engine->RunAsync(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->StageAndResolve();
			});
	}

	void FBatch::StageAndResolve()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectVtx)

		Patcher = MakeShared<PCGExGraphs::FGraphPatcher>(VtxDataFacade);
		RegisterClusterGroups(*Patcher, ValidClusters, 0, INDEX_NONE);
		StageProbeEdges(*Patcher, Engine->GetUniqueEdges(), ExistingEdges, ConnectorEdgeHandles, ConnectorEndpoints);

		if (ConnectorEdgeHandles.IsEmpty() && !Settings->bQuietNoConnectionWarning)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("No connection was created."));
		}

		Patcher->ResolveAndMergeAsync(Context->MainEdges.ToSharedRef(), TaskManager, &Context->CarryOverDetails);
	}

	void FBatch::Write()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectVtx)

		if (!Patcher)
		{
			return;
		}

		Patcher->Commit();

		PCGExGraphs::WriteConnectorFlags(
			*Patcher, VtxDataFacade,
			{Settings->bFlagVtxConnector, Settings->VtxConnectorFlagName, Settings->bFlagEdgeConnector, Settings->EdgeConnectorFlagName},
			ConnectorEdgeHandles, ConnectorEndpoints);
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
