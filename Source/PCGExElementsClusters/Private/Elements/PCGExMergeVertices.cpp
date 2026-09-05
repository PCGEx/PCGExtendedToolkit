// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExMergeVertices.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExClusterData.h"
#include "Utils/PCGExPointIOMerger.h"


#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

PCGExData::EIOInit UPCGExMergeVerticesSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExMergeVerticesSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

#pragma endregion

void FPCGExMergeVerticesContext::ClusterProcessing_InitialProcessingDone()
{
	Merger = MakeShared<FPCGExPointIOMerger>(CompositeDataFacade.ToSharedRef());

	int32 StartOffset = 0;

	for (int i = 0; i < Batches.Num(); i++)
	{
		PCGExClusterMT::TBatch<PCGExMergeVertices::FProcessor>* Batch = static_cast<PCGExClusterMT::TBatch<PCGExMergeVertices::FProcessor>*>(Batches[i].Get());
		Merger->Append(Batch->VtxDataFacade->Source);

		for (int Pi = 0; Pi < Batch->GetNumProcessors(); Pi++)
		{
			Batch->GetProcessor<PCGExMergeVertices::FProcessor>(Pi)->StartIndexOffset = StartOffset;
		}
		StartOffset += Batch->VtxDataFacade->GetNum();
	}

	Merger->MergeAsync(GetTaskManager(), &CarryOverDetails);
	PCGExClusters::Helpers::SetClusterVtx(CompositeDataFacade->Source, OutVtxId); // After merge since it forwards IDs
}

void FPCGExMergeVerticesContext::ClusterProcessing_WorkComplete()
{
	CompositeDataFacade->WriteFastest(GetTaskManager());
}

PCGEX_INITIALIZE_ELEMENT(MergeVertices)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(MergeVertices)

bool FPCGExMergeVerticesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(MergeVertices)

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	TSharedPtr<PCGExData::FPointIO> CompositeIO = PCGExData::NewPointIO(Context, PCGExClusters::Labels::OutputVerticesLabel, 0);
	Context->CompositeDataFacade = MakeShared<PCGExData::FFacade>(CompositeIO.ToSharedRef());
	CompositeIO->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New);

	return true;
}

bool FPCGExMergeVerticesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExMergeVerticesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(MergeVertices)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	(void)Context->CompositeDataFacade->Source->StageOutput(Context);
	Context->MainEdges->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExMergeVertices
{
	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, true, true, true);
	}

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExMergeVertices::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Cluster->WillModifyVtxIO();

		return true;
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];

			Node.PointIndex += StartIndexOffset;
		}
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = ClusterEdges[Index];

			Edge.Start += StartIndexOffset;
			Edge.End += StartIndexOffset;
		}
	}

	void FProcessor::CompleteWork()
	{
		StartParallelLoopForNodes();
		StartParallelLoopForEdges();
	}

	void FProcessor::Write()
	{
		Cluster->VtxIO = Context->CompositeDataFacade->Source;
		Cluster->NumRawVtx = Context->CompositeDataFacade->Source->GetNum(PCGExData::EIOSide::Out);

		PCGEX_INIT_IO_VOID(EdgeDataFacade->Source, PCGExData::EIOInit::Forward)

		PCGExClusters::Helpers::MarkClusterEdges(EdgeDataFacade->Source, Context->OutVtxId);

		ForwardCluster();
	}
}

#undef LOCTEXT_NAMESPACE
