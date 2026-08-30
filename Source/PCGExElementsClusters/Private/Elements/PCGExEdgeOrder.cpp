// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExEdgeOrder.h"


#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExScopedContainers.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"

#define LOCTEXT_NAMESPACE "EdgeOrder"
#define PCGEX_NAMESPACE EdgeOrder

#pragma region UPCGExEdgeOrderSettings

PCGExData::EIOInit UPCGExEdgeOrderSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGExData::EIOInit UPCGExEdgeOrderSettings::GetEdgeOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

TArray<FPCGPinProperties> UPCGExEdgeOrderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (Mode != EPCGExEdgeOrderMode::DirectionSettings)
	{
		PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seed points used as traversal starting positions.", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(EdgeOrder)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(EdgeOrder)

#pragma endregion

#pragma region FPCGExEdgeOrderElement

bool FPCGExEdgeOrderElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(EdgeOrder)

	if (Settings->Mode != EPCGExEdgeOrderMode::DirectionSettings)
	{
		Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
		if (!Context->SeedsDataFacade)
		{
			return false;
		}
	}

	return true;
}

bool FPCGExEdgeOrderElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExEdgeOrderElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(EdgeOrder)
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
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExEdgeOrder::FProcessor

namespace PCGExEdgeOrder
{
	FProcessor::~FProcessor()
	{
	}

	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		// Create a lite copy with only edges edited, we'll forward that to the output
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, false, true, true);
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExEdgeOrder::Process);

		EdgeDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		VtxEndpointBuffer = VtxDataFacade->GetReadable<int64>(PCGExClusters::Labels::Attr_PCGExVtxIdx);
		EndpointsBuffer = EdgeDataFacade->GetWritable<int64>(PCGExClusters::Labels::Attr_PCGExEdgeIdx, PCGExData::EBufferInit::New);

		if (Settings->Mode == EPCGExEdgeOrderMode::DirectionSettings)
		{
			if (!DirectionSettings.InitFromParent(ExecutionContext, GetParentBatch<FBatch>()->DirectionSettings, EdgeDataFacade))
			{
				return false;
			}

			StartParallelLoopForEdges();
			return true;
		}

		// DFS traversal path
		if (Context->SeedsDataFacade->GetNum() <= 0)
		{
			return false;
		}

		Depths.Init(-1, NumNodes);
		Seeded.Init(0, NumNodes);

		if (Settings->bUseOctreeSearch)
		{
			Cluster->RebuildOctree(Settings->SeedPicking.PickingMethod);
		}

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, SeedPickingGroup)

		SeedPickingGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->RunDFS();
		};

		SeedPickingGroup->OnPrepareSubLoopsCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TArray<PCGExMT::FScope>& Loops)
		{
			PCGEX_ASYNC_THIS
			This->SeedNodeIndices = MakeShared<PCGExMT::TScopedArray<int32>>(Loops);
		};

		SeedPickingGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS

			TConstPCGValueRange<FTransform> SeedTransforms = This->Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();

			PCGEX_SCOPE_LOOP(Index)
			{
				const FVector SeedLocation = SeedTransforms[Index].GetLocation();
				const int32 ClosestIndex = This->Settings->SeedPicking.PickClosestNode(*This->Cluster, SeedLocation);

				if (ClosestIndex < 0)
				{
					continue;
				}

				if (FPlatformAtomics::InterlockedCompareExchange(&This->Seeded[ClosestIndex], 1, 0) == 1)
				{
					continue;
				}

				This->SeedNodeIndices->Get(Scope)->Add(ClosestIndex);
			}
		};

		SeedPickingGroup->StartSubLoops(Context->SeedsDataFacade->GetNum(), PCGEX_CORE_SETTINGS.ClusterDefaultBatchChunkSize);

		return true;
	}

	void FProcessor::RunDFS()
	{
		SeedNodeIndices->Collapse(CollectedSeeds);
		SeedNodeIndices.Reset();

		if (CollectedSeeds.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("A cluster could not match any seed points. Check seed positions and picking distance."));
			bIsProcessorValid = false;
			return;
		}

		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		// Iterative DFS: stack discipline gives each reached node its DFS tree depth.
		// Depth strictly increases along the discovery path, so non-tree edges (back/cross)
		// always connect nodes at different depths -- orientation is unambiguous.
		TArray<int32> Stack;
		Stack.Reserve(Nodes.Num());

		for (const int32 SeedNodeIdx : CollectedSeeds)
		{
			Depths[SeedNodeIdx] = 0;
			Stack.Add(SeedNodeIdx);
		}

		while (!Stack.IsEmpty())
		{
			const int32 CurrentIdx = Stack.Pop(EAllowShrinking::No);
			const int32 NextDepth = Depths[CurrentIdx] + 1;

			for (const PCGExGraphs::FLink& Lk : Nodes[CurrentIdx].Links)
			{
				if (Depths[Lk.Node] != -1)
				{
					continue;
				}
				Depths[Lk.Node] = NextDepth;
				Stack.Add(Lk.Node);
			}
		}

		StartParallelLoopForEdges();
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		EdgeDataFacade->Fetch(Scope);

		TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;

		if (Settings->Mode == EPCGExEdgeOrderMode::DirectionSettings)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				PCGExGraphs::FEdge& Edge = ClusterEdges[Index];

				DirectionSettings.SortEndpoints(Cluster.Get(), Edge);

				uint32 StartID = 0;
				uint32 StartAdjacency = 0;
				PCGEx::H64(VtxEndpointBuffer->Read(Edge.Start), StartID, StartAdjacency);

				uint32 EndID = 0;
				uint32 EndAdjacency = 0;
				PCGEx::H64(VtxEndpointBuffer->Read(Edge.End), EndID, EndAdjacency);

				EndpointsBuffer->SetValue(Index, PCGEx::H64(StartID, EndID)); // Rewrite endpoints data as ordered
			}

			return;
		}

		// DFS path: orient each edge from lower-depth endpoint to higher-depth endpoint.
		// Unreached edges (one or both endpoints with depth == -1) or equal-depth edges pass through.
		const bool bInvert = Settings->bInvert;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = ClusterEdges[Index];

			const int32 StartNodeIdx = Cluster->NodeIndexLookup->Get(Edge.Start);
			const int32 EndNodeIdx = Cluster->NodeIndexLookup->Get(Edge.End);

			const int32 StartDepth = Depths[StartNodeIdx];
			const int32 EndDepth = Depths[EndNodeIdx];

			bool bSwap = false;
			if (StartDepth >= 0 && EndDepth >= 0 && StartDepth != EndDepth)
			{
				bSwap = (StartDepth > EndDepth);
				if (bInvert)
				{
					bSwap = !bSwap;
				}
			}

			if (bSwap)
			{
				const uint32 Tmp = Edge.Start;
				Edge.Start = Edge.End;
				Edge.End = Tmp;
			}

			uint32 StartID = 0;
			uint32 StartAdjacency = 0;
			PCGEx::H64(VtxEndpointBuffer->Read(Edge.Start), StartID, StartAdjacency);

			uint32 EndID = 0;
			uint32 EndAdjacency = 0;
			PCGEx::H64(VtxEndpointBuffer->Read(Edge.End), EndID, EndAdjacency);

			EndpointsBuffer->SetValue(Index, PCGEx::H64(StartID, EndID));
		}
	}

	void FProcessor::CompleteWork()
	{
		EdgeDataFacade->WriteFastest(TaskManager);
		ForwardCluster();
	}

#pragma endregion

#pragma region PCGExEdgeOrder::FBatch

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(EdgeOrder)
		FacadePreloader.Register<int64>(ExecutionContext, PCGExClusters::Labels::Attr_PCGExVtxIdx);

		if (Settings->Mode == EPCGExEdgeOrderMode::DirectionSettings)
		{
			DirectionSettings.RegisterBuffersDependencies(ExecutionContext, FacadePreloader);
		}
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(EdgeOrder)

		if (Settings->Mode == EPCGExEdgeOrderMode::DirectionSettings)
		{
			DirectionSettings = Settings->DirectionSettings;

			if (!DirectionSettings.Init(ExecutionContext, VtxDataFacade, Context->GetEdgeSortingRules()))
			{
				bIsBatchValid = false;
				return;
			}
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
