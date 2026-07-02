// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExFilterVtx.h"


#include "PCGExVersion.h"
#include "Async/ParallelFor.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExGraphCommon.h"

#define LOCTEXT_NAMESPACE "PCGExFilterVtx"
#define PCGEX_NAMESPACE FilterVtx

#if WITH_EDITOR
void UPCGExFilterVtxSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 70, 11)
	{
		ResultOutputVtx.ResultAttributeName = ResultAttributeName_DEPRECATED;
		ResultAttributeName_DEPRECATED = NAME_None;
	}

	PCGEX_IF_VERSION_LOWER(1, 71, 2)
	{
		ResultOutputVtx.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TArray<FPCGPinProperties> UPCGExFilterVtxSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceVtxFiltersLabel, "Vtx filters.", Required)

	if (Mode == EPCGExVtxFilterOutput::Clusters)
	{
		PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceEdgeFiltersLabel, "Optional Edge filters. Selected edges will be invalidated, possibly pruning more vtx along the way.", Normal)
	}

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFilterVtxSettings::OutputPinProperties() const
{
	if (Mode != EPCGExVtxFilterOutput::Points)
	{
		return Super::OutputPinProperties();
	}

	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExFilters::Labels::OutputInsideFiltersLabel, "Vtx points that passed the filters.", Required)
	PCGEX_PIN_POINTS(PCGExFilters::Labels::OutputOutsideFiltersLabel, "Vtx points that didn't pass the filters.", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExFilterVtxSettings::GetMainOutputInitMode() const
{
	switch (Mode)
	{
	default:
	case EPCGExVtxFilterOutput::Clusters:
		return PCGExData::EIOInit::New;
	case EPCGExVtxFilterOutput::Points:
		return PCGExData::EIOInit::NoInit;
	case EPCGExVtxFilterOutput::Attribute:
		return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
	}
}

PCGExData::EIOInit UPCGExFilterVtxSettings::GetEdgeOutputInitMode() const
{
	switch (Mode)
	{
	default:
	case EPCGExVtxFilterOutput::Attribute:
	case EPCGExVtxFilterOutput::Clusters:
		return PCGExData::EIOInit::Forward;
	case EPCGExVtxFilterOutput::Points:
		return PCGExData::EIOInit::NoInit;
	}
}

PCGEX_INITIALIZE_ELEMENT(FilterVtx)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(FilterVtx)

bool FPCGExFilterVtxElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FilterVtx)

	Context->bWantsClusters = Settings->Mode != EPCGExVtxFilterOutput::Points;

	PCGEX_FWD(GraphBuilderDetails)

	if (!PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceVtxFiltersLabel, Context->VtxFilterFactories, PCGExFactories::ClusterNodeFilters()))
	{
		return false;
	}

	if (Settings->Mode == EPCGExVtxFilterOutput::Clusters)
	{
		PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceEdgeFiltersLabel, Context->EdgeFilterFactories, PCGExFactories::ClusterEdgeFilters(), false);
	}

	if (!Context->bWantsClusters)
	{
		Context->Inside = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->Outside = MakeShared<PCGExData::FPointIOCollection>(Context);

		Context->Inside->OutputPin = PCGExFilters::Labels::OutputInsideFiltersLabel;
		Context->Outside->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;

		if (Settings->bSwap)
		{
			Context->Inside->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;
			Context->Outside->OutputPin = PCGExFilters::Labels::OutputInsideFiltersLabel;
		}
	}

	// TODO : Offer to output discarded vtxs in a third output when using cluster mode

	return true;
}

bool FPCGExFilterVtxElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFilterVtxElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FilterVtx)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->GraphBuilderDetails = Context->GraphBuilderDetails;
				NewBatch->VtxFilterFactories = &Context->VtxFilterFactories;
				if (!Context->EdgeFilterFactories.IsEmpty())
				{
					NewBatch->EdgeFilterFactories = &Context->EdgeFilterFactories;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(Settings->Mode == EPCGExVtxFilterOutput::Clusters ? PCGExGraphs::States::State_ReadyToCompile : PCGExCommon::States::State_Done)

	if (Settings->Mode == EPCGExVtxFilterOutput::Clusters)
	{
		if (!Context->CompileGraphBuilders(true, PCGExCommon::States::State_Done))
		{
			return false;
		}
		Context->MainPoints->StageOutputs();
	}
	else if (Settings->Mode == EPCGExVtxFilterOutput::Attribute)
	{
		Context->OutputPointsAndEdges();
	}
	else
	{
		Context->Inside->StageOutputs();
		Context->Outside->StageOutputs();
	}

	return Context->TryComplete();
}

namespace PCGExFilterVtx
{
	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		// Create a light working copy with nodes only, will be deleted.
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, true, false, false);
	}

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFilterVtx::Process);

		bAllowEdgesDataFacadeScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (!VtxFiltersManager)
		{
			// Log error that filters shouldn't be empty
			// Also boot should've thrown :/
			return false;
		}

		if (Settings->Mode == EPCGExVtxFilterOutput::Attribute)
		{
			ResultOutputVtx = StaticCastSharedPtr<FBatch>(ParentBatch.Pin())->ResultOutputVtx;
		}

		StartParallelLoopForNodes();
		if (!Context->EdgeFilterFactories.IsEmpty())
		{
			StartParallelLoopForEdges();
		}

		return true;
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		TArray<PCGExClusters::FEdge>& Edges = *Cluster->Edges.Get();

		const bool bNodeInvalidateEdges = Settings->bNodeInvalidateEdges;
		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];
			Node.bValid = VtxFiltersManager->Test(Node) ? !Settings->bInvert : Settings->bInvert;
			if (!Node.bValid && bNodeInvalidateEdges)
			{
				for (const PCGExGraphs::FLink& Lk : Node.Links)
				{
					FPlatformAtomics::InterlockedExchange(&Edges[Lk.Edge].bValid, 0);
				}
			}
		}
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		EdgeDataFacade->Fetch(Scope);
		FilterEdgeScope(Scope);

		TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = ClusterEdges[Index];
			Edge.bValid = EdgeFilterCache[Index] ? !Settings->bInvertEdgeFilters : Settings->bInvertEdgeFilters;
		}
	}

	void FProcessor::CompleteWork()
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();
		TArray<PCGExClusters::FEdge>& Edges = *Cluster->Edges.Get();

		PassNum = 0;
		FailNum = 0;

		if (Nodes.Num() > 1024)
		{
			ParallelFor(Nodes.Num(), [&](const int32 i)
			{
				PCGExClusters::FNode& Node = Nodes[i];
				if (Node.bValid)
				{
					int32 ValidCount = 0;
					for (const PCGExGraphs::FLink& Lk : Node.Links)
					{
						ValidCount += Edges[Lk.Edge].bValid;
					}
					Node.bValid = static_cast<bool>(ValidCount);
				}

				if (Node.bValid)
				{
					FPlatformAtomics::InterlockedIncrement(&PassNum);
				}
				else
				{
					FPlatformAtomics::InterlockedIncrement(&FailNum);
				}
			});
		}
		else
		{
			for (PCGExClusters::FNode& Node : Nodes)
			{
				if (Node.bValid)
				{
					int32 ValidCount = 0;
					for (const PCGExGraphs::FLink& Lk : Node.Links)
					{
						ValidCount += Edges[Lk.Edge].bValid;
					}
					Node.bValid = static_cast<bool>(ValidCount);
				}

				if (Node.bValid)
				{
					PassNum++;
				}
				else
				{
					FailNum++;
				}
			}
		}

		if (Settings->Mode == EPCGExVtxFilterOutput::Attribute)
		{
			for (PCGExClusters::FNode& Node : Nodes)
			{
				ResultOutputVtx.Write(Node.PointIndex, static_cast<bool>(Node.bValid));
				Node.bValid = true;
			}

			for (PCGExClusters::FEdge& Edge : Edges)
			{
				Edge.bValid = true;
			}

			return;
		}

		if (Settings->Mode == EPCGExVtxFilterOutput::Clusters)
		{
			TArray<PCGExGraphs::FEdge> ValidEdges;
			Cluster->GetValidEdges(ValidEdges);

			if (ValidEdges.IsEmpty())
			{
				return;
			}
			ValidEdges.Sort();

			GraphBuilder->Graph->InsertEdges(ValidEdges);
		}
		else if (Settings->Mode == EPCGExVtxFilterOutput::Points)
		{
			TArray<int32> ReadIndices;

			if (PassNum == 0 || FailNum == 0)
			{
				// Create a single partition in the right bucket

				const TSharedPtr<PCGExData::FPointIO> OutIO = (PassNum == 0 ? Context->Outside : Context->Inside)->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);

				if (!OutIO)
				{
					return;
				}

				PCGExClusters::Helpers::CleanupVtxData(OutIO);

				(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutIO->GetOut(), NumNodes, OutIO->GetAllocations());
				OutIO->IOIndex = VtxDataFacade->Source->IOIndex * 100000 + BatchIndex;

				ReadIndices.SetNumUninitialized(NumNodes);
				for (int i = 0; i < NumNodes; i++)
				{
					ReadIndices[i] = Nodes[i].PointIndex;
				}

				ReadIndices.Sort();
				OutIO->InheritPoints(ReadIndices, 0);

				return;
			}

			const TSharedPtr<PCGExData::FPointIO> Inside = Context->Inside->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
			const TSharedPtr<PCGExData::FPointIO> Outside = Context->Outside->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);

			if (!Inside || !Outside)
			{
				return;
			}

			PCGExClusters::Helpers::CleanupVtxData(Inside);
			PCGExClusters::Helpers::CleanupVtxData(Outside);

			Inside->IOIndex = VtxDataFacade->Source->IOIndex * 100000 + BatchIndex;
			Outside->IOIndex = VtxDataFacade->Source->IOIndex * 100000 + BatchIndex;

			auto GatherNodes = [&](const TSharedPtr<PCGExData::FPointIO>& IO, const bool bValid)
			{
				Cluster->GatherNodesPointIndices(ReadIndices, bValid);
				ReadIndices.Sort();
				(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(IO->GetOut(), ReadIndices.Num());
				IO->InheritPoints(ReadIndices, 0);
			};

			GatherNodes(Inside, true);
			GatherNodes(Outside, false);
		}
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(FilterVtx)

		if (Settings->Mode == EPCGExVtxFilterOutput::Attribute)
		{
			ResultOutputVtx = Settings->ResultOutputVtx;
			ResultOutputVtx.Init(VtxDataFacade);
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	void FBatch::CompleteWork()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(FilterVtx)

		if (Context->bWantsClusters || Settings->bSplitOutputsByConnectivity)
		{
			TBatch<FProcessor>::CompleteWork();
			return;
		}

		// Since we don't split outputs by connectivity, we can handle filtering here directly without
		// going back to processors

		int32 PassNum = 0;
		int32 FailNum = 0;

		for (int Pi = 0; Pi < Processors.Num(); Pi++)
		{
			const TSharedPtr<FProcessor> P = GetProcessor<FProcessor>(Pi);
			PassNum += P->PassNum;
			FailNum += P->FailNum;
		}

		if (PassNum == 0 || FailNum == 0)
		{
			// Just duplicate vtx points into the right bucket

			const TSharedPtr<PCGExData::FPointIO> OutIO = (PassNum == 0 ? Context->Outside : Context->Inside)->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::Duplicate);

			if (!OutIO)
			{
				return;
			}

			PCGExClusters::Helpers::CleanupVtxData(OutIO);

			return;
		}

		// Distribute points to partitions

		const TSharedPtr<PCGExData::FPointIO> Inside = Context->Inside->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
		const TSharedPtr<PCGExData::FPointIO> Outside = Context->Outside->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);

		if (!Inside || !Outside)
		{
			return;
		}

		PCGExClusters::Helpers::CleanupVtxData(Inside);
		PCGExClusters::Helpers::CleanupVtxData(Outside);

		TArray<int8> Mask;
		Mask.SetNumUninitialized(VtxDataFacade->GetNum(PCGExData::EIOSide::In));

		for (int Pi = 0; Pi < Processors.Num(); Pi++)
		{
			const TSharedPtr<FProcessor> P = GetProcessor<FProcessor>(Pi);
			for (const TArray<PCGExClusters::FNode>& Nodes = *P->Cluster->Nodes.Get();
			     const PCGExClusters::FNode& Node : Nodes)
			{
				Mask[Node.PointIndex] = Node.bValid ? 1 : 0;
			}
		}

		Inside->IOIndex = VtxDataFacade->Source->IOIndex;
		Outside->IOIndex = VtxDataFacade->Source->IOIndex;

		(void)Inside->InheritPoints(Mask, false);
		(void)Outside->InheritPoints(Mask, true);
	}

	void FBatch::Write()
	{
		VtxDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
