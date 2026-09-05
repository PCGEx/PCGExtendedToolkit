// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPartitionVertices.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

PCGExData::EIOInit UPCGExPartitionVerticesSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExPartitionVerticesSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

#pragma endregion

PCGEX_INITIALIZE_ELEMENT(PartitionVertices)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(PartitionVertices)

bool FPCGExPartitionVerticesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PartitionVertices)

	Context->VtxPartitions = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->VtxPartitions->OutputPin = PCGExClusters::Labels::OutputVerticesLabel;

	return true;
}

bool FPCGExPartitionVerticesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPartitionVerticesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PartitionVertices)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}

		Context->VtxPartitions->Pairs.Reserve(Context->GetClusterProcessorsNum());
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputBatches();

	Context->VtxPartitions->PruneNullEntries(false);
	Context->VtxPartitions->StageOutputs();

	Context->MainEdges->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExPartitionVertices
{
	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, true, true, true);
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPartitionVertices::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PointPartitionIO = Context->VtxPartitions->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
		UPCGBasePointData* MutablePoints = PointPartitionIO->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumNodes, VtxDataFacade->GetAllocations());

		TMap<int32, int32> EndpointsMap;
		EndpointsMap.Reserve(NumNodes);

		TArray<int32> VtxSelection;
		VtxSelection.SetNumUninitialized(NumNodes);


		Cluster->WillModifyVtxIO();

		Cluster->VtxIO = PointPartitionIO;
		Cluster->NumRawVtx = NumNodes;

		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		for (PCGExClusters::FNode& Node : Nodes)
		{
			int32 i = Node.Index;

			VtxSelection[i] = Node.PointIndex;
			EndpointsMap.Add(Node.PointIndex, i);
			Node.PointIndex = i;
		}

		TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;
		for (PCGExGraphs::FEdge& Edge : Edges)
		{
			Edge.Start = EndpointsMap[Edge.Start];
			Edge.End = EndpointsMap[Edge.End];
		}

		PointPartitionIO->InheritPoints(VtxSelection, 0);
		return true;
	}

	void FProcessor::CompleteWork()
	{
		PCGExDataId OutId;
		PCGExClusters::Helpers::SetClusterVtx(PointPartitionIO, OutId);
		PCGExClusters::Helpers::MarkClusterEdges(EdgeDataFacade->Source, OutId);

		ForwardCluster();
	}
}

#undef LOCTEXT_NAMESPACE
