// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExRepairClusters.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

PCGExData::EIOInit UPCGExSanitizeClustersSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExSanitizeClustersSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

#pragma endregion
TArray<FText> UPCGExSanitizeClustersSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Cluster : Sanitize")};
}

FPCGElementPtr UPCGExSanitizeClustersSettings::CreateElement() const{ return MakeShared<FPCGExRepairClustersElement>();}
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(RepairClusters)

bool FPCGExRepairClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT(RepairClusters) 
	PCGEX_SETTINGS(SanitizeClusters)
	PCGEX_FWD(GraphBuilderDetails)

	return true;
}

bool FPCGExRepairClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSanitizeClustersElement::Execute);

	PCGEX_CONTEXT(RepairClusters) 
	PCGEX_SETTINGS(SanitizeClusters)
	
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->GraphBuilderDetails = Context->GraphBuilderDetails;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputBatches();
	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExRepairClusters
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSanitizeClusters::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		TArray<PCGExGraphs::FEdge> IndexedEdges;

		PCGExGraphs::Helpers::BuildIndexedEdges(EdgeDataFacade->Source, *EndpointsLookup, IndexedEdges);

		if (IndexedEdges.IsEmpty())
		{
			return false;
		}

		GraphBuilder->Graph->InsertEdges(IndexedEdges);
		EdgeDataFacade->Source->ClearCachedKeys();
		return true;
	}

	void FBatch::OnInitialPostProcess()
	{
		TBatch<FProcessor>::OnInitialPostProcess();
		GraphBuilder->Compile(TaskManager, true);
	}

	void FBatch::Output()
	{
		if (GraphBuilder->bCompiledSuccessfully)
		{
			GraphBuilder->StageEdgesOutputs();
		}
		else
		{
			VtxDataFacade->Source->InitializeOutput(PCGExData::EIOInit::NoInit);
		}
	}
}

#undef LOCTEXT_NAMESPACE
