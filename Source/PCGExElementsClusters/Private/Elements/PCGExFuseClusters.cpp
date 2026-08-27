// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFuseClusters.h"


#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExUnionData.h"
#include "Core/PCGExUnionRegistry.h"
#include "Core/PCGExUnionTable.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphHelpers.h"
#include "Graphs/Union/PCGExIntersections.h"
#include "Graphs/Union/PCGExUnionProcessor.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

PCGExData::EIOInit UPCGExFuseClustersSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFuseClustersSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

#pragma endregion

PCGEX_INITIALIZE_ELEMENT(FuseClusters)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FuseClusters)

bool FPCGExFuseClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FuseClusters)

	PCGEX_FWD(VtxCarryOverDetails)
	Context->VtxCarryOverDetails.Init();

	PCGEX_FWD(EdgesCarryOverDetails)
	Context->EdgesCarryOverDetails.Init();

	const_cast<UPCGExFuseClustersSettings*>(Settings)->EdgeEdgeIntersectionDetails.Init();

	const TSharedPtr<PCGExData::FPointIO> UnionIO = PCGExData::NewPointIO(Context, PCGExClusters::Labels::OutputVerticesLabel);
	UnionIO->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New);

	Context->UnionDataFacade = MakeShared<PCGExData::FFacade>(UnionIO.ToSharedRef());

	// Phase 1+2 streaming build state. FuseDetails is stashed on the context so processors can
	// look up grid keys / octree tolerance without going through the settings each call.
	Context->FuseDetails = Settings->PointPointIntersectionDetails.FuseDetails;
	// TODO : Support local fuse distance, requires access to all input facades
	if (!Context->FuseDetails.Init(Context, nullptr))
	{
		return false;
	}

	Context->FuseBounds = Context->MainPoints->GetInBounds().ExpandBy(10);
	Context->bUseOctreeMode = (Context->FuseDetails.GetEffectiveMethod() == EPCGExFuseMethod::Octree);

	Context->NodeBuilder = MakeShared<PCGExData::FUnionTableBuilder>(1);
	Context->NodeBuilder->bDedupeElementsBySource = true; // node table: collapse shared-vtx duplicates
	Context->EdgeBuilder = MakeShared<PCGExData::FUnionTableBuilder>(1);
	if (Context->bUseOctreeMode)
	{
		Context->NodeRegistry = MakeShared<PCGExData::FUnionRegistry>(Context->FuseBounds);
	}

	Context->UnionProcessor = MakeShared<PCGExGraphs::FUnionProcessor>(Context, Context->UnionDataFacade.ToSharedRef(), Settings->PointPointIntersectionDetails, Settings->DefaultPointsBlendingDetails, Settings->DefaultEdgesBlendingDetails);

	Context->UnionProcessor->VtxCarryOverDetails = &Context->VtxCarryOverDetails;
	Context->UnionProcessor->EdgesCarryOverDetails = &Context->EdgesCarryOverDetails;

	if (Settings->bFindPointEdgeIntersections)
	{
		Context->UnionProcessor->InitPointEdge(Settings->PointEdgeIntersectionDetails, Settings->bUseCustomPointEdgeBlending, &Settings->CustomPointEdgeBlendingDetails);
	}

	if (Settings->bFindEdgeEdgeIntersections)
	{
		Context->UnionProcessor->InitEdgeEdge(Settings->EdgeEdgeIntersectionDetails, Settings->bUseCustomEdgeEdgeBlending, &Settings->CustomEdgeEdgeBlendingDetails);
	}

	return true;
}

bool FPCGExFuseClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFuseClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FuseClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		const bool bUseOctree = Context->bUseOctreeMode;
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [bUseOctree](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
				// Octree-fuse mode routes through FUnionRegistry, which is sequential by contract.
				// Grid mode is fully parallel: each processor builds local records, then the post-batch
				// step collects and sort-groups them deterministically.
				NewBatch->bForceSingleThreadedProcessing = bUseOctree;
			}, true))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExGraphs::States::State_PreparingUnion)

	PCGEX_ON_STATE(PCGExGraphs::States::State_PreparingUnion)
	{
		using namespace PCGExFuseClusters;

		const int32 NumFacades = Context->Batches.Num();

		Context->VtxFacades.Reserve(NumFacades);
		Context->UnionProcessor->SourceEdgesIO = &Context->EdgesDataFacades;

		// Phase 1+2 collection: walk all batches/processors in stable order, append per-processor
		// node records and staged edges into the central builders. Order is deterministic regardless
		// of how processors ran in parallel because we serialize the merge here.
		TArray<PCGExData::FUnionStreamRecord>& NodeScope = Context->NodeBuilder->GetScope(0);
		TArray<FStagedEdge> AllStagedEdges;

		int32 EstNodeRecords = 0;
		int32 EstStagedEdges = 0;
		for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Context->Batches)
		{
			Context->VtxFacades.Add(Batch->VtxDataFacade);
			const int32 NumProcs = Batch->GetNumProcessors();
			for (int32 i = 0; i < NumProcs; i++)
			{
				const TSharedPtr<FProcessor> Proc = Batch->GetProcessor<FProcessor>(i);
				if (!Proc.IsValid())
				{
					continue;
				}
				EstNodeRecords += Proc->NodeRecords.Num();
				EstStagedEdges += Proc->StagedEdges.Num();
			}
		}
		NodeScope.Reserve(EstNodeRecords);
		AllStagedEdges.Reserve(EstStagedEdges);

		for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Context->Batches)
		{
			const int32 NumProcs = Batch->GetNumProcessors();
			for (int32 i = 0; i < NumProcs; i++)
			{
				const TSharedPtr<FProcessor> Proc = Batch->GetProcessor<FProcessor>(i);
				if (!Proc.IsValid() || Proc->bInvalidEdges)
				{
					continue;
				}
				NodeScope.Append(MoveTemp(Proc->NodeRecords));
				AllStagedEdges.Append(MoveTemp(Proc->StagedEdges));
			}
		}

		// Phase 1 -- compile node table
		const TSharedPtr<PCGExData::FUnionTable> NodesTable = MakeShared<PCGExData::FUnionTable>();
		Context->NodeBuilder->Compile(*NodesTable);
		Context->NodeBuilder.Reset();

		const int32 NumUnionNodes = NodesTable->Num();
		if (NumUnionNodes == 0)
		{
			return Context->CancelExecution(TEXT("Union table is empty after fuse build."));
		}

		// Build (Key -> NodeIndex) lookup so staged edges can resolve their endpoints. Keys come
		// straight from NodesTable; size is exactly NumUnionNodes since the table is one-entry-per-key.
		TMap<uint64, int32> KeyToNode;
		KeyToNode.Reserve(NumUnionNodes);
		for (int32 i = 0; i < NumUnionNodes; i++)
		{
			KeyToNode.Add(NodesTable->Keys[i], i);
		}

		// Phase 2 -- emit edge records keyed by packed (min(Start,End), max(Start,End)). The packed
		// form is reversible (high 32 bits = min, low 32 bits = max) so we can decode endpoints
		// from the compiled edge table without a sidecar array.
		TArray<PCGExData::FUnionStreamRecord>& EdgeScope = Context->EdgeBuilder->GetScope(0);
		EdgeScope.Reserve(AllStagedEdges.Num());
		for (const FStagedEdge& Staged : AllStagedEdges)
		{
			const int32* NodeA = KeyToNode.Find(Staged.KeyA);
			const int32* NodeB = KeyToNode.Find(Staged.KeyB);
			if (!NodeA || !NodeB || *NodeA == *NodeB)
			{
				continue;
			} // self-collapsed edge
			const uint64 EdgeKey = PCGEx::H64U(static_cast<uint32>(*NodeA), static_cast<uint32>(*NodeB));
			EdgeScope.Emplace(EdgeKey, Staged.IO, Staged.EdgePointIndex);
		}
		AllStagedEdges.Empty();

		const TSharedPtr<PCGExData::FUnionTable> EdgesTable = MakeShared<PCGExData::FUnionTable>();
		Context->EdgeBuilder->Compile(*EdgesTable);
		Context->EdgeBuilder.Reset();

		// Decode edges from EdgesTable.Keys to a flat FEdge array. AdoptEdges takes ownership and
		// builds adjacency / UniqueEdges / EdgeMetadata. Sub-edge IOIndex follows the legacy
		// behavior (-1) -- known issue, will be addressed in a separate pass.
		const int32 NumEdges = EdgesTable->Num();
		TArray<PCGExGraphs::FEdge> Edges;
		Edges.SetNumUninitialized(NumEdges);
		for (int32 i = 0; i < NumEdges; i++)
		{
			const uint64 K = EdgesTable->Keys[i];
			const int32 Start = static_cast<int32>(PCGEx::H64A(K));
			const int32 End = static_cast<int32>(PCGEx::H64B(K));
			Edges[i] = PCGExGraphs::FEdge(i, Start, End, -1, -1);
		}

		Context->UnionProcessor->SetUnionData(NodesTable, EdgesTable, MoveTemp(Edges), Context->FuseBounds);

		if (!Context->UnionProcessor->StartExecution(Context->VtxFacades, Settings->GraphBuilderDetails))
		{
			return Context->CancelExecution(TEXT("Could not start union."));
		}

		Context->NodeRegistry.Reset();
	}

	if (!Context->UnionProcessor->Execute())
	{
		return false;
	}

	(void)Context->UnionDataFacade->Source->StageOutput(Context);
	Context->Done();

	return Context->TryComplete();
}

namespace PCGExFuseClusters
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFuseClusters::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		VtxIOIndex = VtxDataFacade->Source->IOIndex;
		EdgesIOIndex = EdgeDataFacade->Source->IOIndex;

		Cluster = PCGExClusters::Helpers::TryGetCachedCluster(VtxDataFacade->Source, EdgeDataFacade->Source);

		if (!Cluster)
		{
			if (!PCGExGraphs::Helpers::BuildIndexedEdges(EdgeDataFacade->Source, *EndpointsLookup, IndexedEdges, true))
			{
				return false;
			}
			if (IndexedEdges.IsEmpty())
			{
				return false;
			}
		}
		else
		{
			NumNodes = Cluster->Nodes->Num();
			NumEdges = Cluster->Edges->Num();
		}

		bInvalidEdges = false;

		const int32 NumIterations = Cluster ? Cluster->Edges->Num() : IndexedEdges.Num();
		// Each edge contributes 2 node records (endpoints) and 1 staged edge.
		NodeRecords.Reserve(NumIterations * 2);
		StagedEdges.Reserve(NumIterations);

		EmitEdges(PCGExMT::FScope(0, NumIterations));

		return true;
	}

	void FProcessor::EmitEdges(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFuseClusters::FProcessor::EmitEdges);

		const FPCGExFuseDetails& FuseDetails = Context->FuseDetails;
		const bool bUseOctree = Context->bUseOctreeMode;
		const TSharedPtr<PCGExData::FUnionRegistry> Registry = Context->NodeRegistry;

		auto EmitEdge = [&](const PCGExData::FConstPoint& From, const PCGExData::FConstPoint& To, const PCGExData::FConstPoint& EdgePt)
		{
			uint64 KeyA;
			uint64 KeyB;
			if (bUseOctree)
			{
				// FUnionRegistry::FindOrInsert is sequential by contract; safe because the cluster
				// batch runs single-threaded in octree mode.
				KeyA = static_cast<uint64>(Registry->FindOrInsert(From, FuseDetails));
				KeyB = static_cast<uint64>(Registry->FindOrInsert(To, FuseDetails));
			}
			else
			{
				KeyA = FuseDetails.GetGridKey(From.GetLocation(), From.Index);
				KeyB = FuseDetails.GetGridKey(To.GetLocation(), To.Index);
			}

			NodeRecords.Emplace(KeyA, From.IO, From.Index);
			NodeRecords.Emplace(KeyB, To.IO, To.Index);
			StagedEdges.Emplace(KeyA, KeyB, EdgePt.IO, EdgePt.Index);
		};

		if (Cluster)
		{
			PCGEX_SCOPE_LOOP(i)
			{
				const PCGExGraphs::FEdge* Edge = Cluster->GetEdge(i);
				EmitEdge(VtxDataFacade->GetInPoint(Edge->Start), VtxDataFacade->GetInPoint(Edge->End), EdgeDataFacade->GetInPoint(Edge->PointIndex));
			}
		}
		else
		{
			PCGEX_SCOPE_LOOP(i)
			{
				const PCGExGraphs::FEdge& Edge = IndexedEdges[i];
				EmitEdge(VtxDataFacade->GetInPoint(Edge.Start), VtxDataFacade->GetInPoint(Edge.End), EdgeDataFacade->GetInPoint(Edge.PointIndex));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
