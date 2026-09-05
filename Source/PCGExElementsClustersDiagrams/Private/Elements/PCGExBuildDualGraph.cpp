// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildDualGraph.h"

#include "Blenders/PCGExUnionBlender.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Graphs/PCGExSubGraph.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/PCGExMathDistances.h"
#include "Sampling/PCGExSamplingUnionData.h"

#define LOCTEXT_NAMESPACE "PCGExBuildDualGraph"
#define PCGEX_NAMESPACE BuildDualGraph

PCGExData::EIOInit UPCGExBuildDualGraphSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExBuildDualGraphSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(BuildDualGraph)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(BuildDualGraph)

bool FPCGExBuildDualGraphElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BuildDualGraph)

	PCGEX_FWD(VtxCarryOverDetails)
	PCGEX_FWD(EdgeCarryOverDetails)
	Context->VtxCarryOverDetails.Init();
	Context->EdgeCarryOverDetails.Init();

	return true;
}

bool FPCGExBuildDualGraphElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildDualGraphElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildDualGraph)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}


namespace PCGExBuildDualGraph
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildDualGraph::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const TArray<PCGExGraphs::FEdge>& ClusterEdges = *Cluster->Edges;

		// Build edge lookup: H64U(nodeA, nodeB) -> dual vertex index
		TMap<uint64, int32> EdgeToDualVtx;
		EdgeToDualVtx.Reserve(NumEdges);

		NumValidEdges = 0;
		for (const PCGExGraphs::FEdge& E : ClusterEdges)
		{
			if (!E.bValid)
			{
				continue;
			}
			const int32 NodeA = Cluster->NodeIndexLookup->Get(E.Start);
			const int32 NodeB = Cluster->NodeIndexLookup->Get(E.End);
			EdgeToDualVtx.Add(PCGEx::H64U(NodeA, NodeB), NumValidEdges++);
		}

		if (NumValidEdges < 2)
		{
			bIsProcessorValid = false;
			return true;
		}

		// Build DCEL. Project() reads ProjectionQuat, which only Init writes, so the settings struct
		// has to be initialized against the data before it is handed over.
		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(VtxDataFacade))
		{
			bIsProcessorValid = false;
			return true;
		}

		FaceEnumerator = MakeShared<PCGExClusters::FPlanarFaceEnumerator>();
		FaceEnumerator->Build(Cluster.ToSharedRef(), ProjectionDetails);

		if (!FaceEnumerator->IsBuilt())
		{
			bIsProcessorValid = false;
			return true;
		}

		// Build dual edges via DCEL half-edge traversal
		// Track shared node's point index for each dual edge (for vertex→edge blending)
		for (const PCGExClusters::FHalfEdge& HE : FaceEnumerator->GetHalfEdges())
		{
			if (HE.NextIndex < 0)
			{
				continue;
			}

			const PCGExClusters::FHalfEdge& NextHE = FaceEnumerator->GetHalfEdge(HE.NextIndex);

			const int32* VtxA = EdgeToDualVtx.Find(PCGEx::H64U(HE.OriginNode, HE.TargetNode));
			const int32* VtxB = EdgeToDualVtx.Find(PCGEx::H64U(NextHE.OriginNode, NextHE.TargetNode));

			if (!VtxA || !VtxB || *VtxA == *VtxB)
			{
				continue;
			}

			const uint64 DualHash = PCGEx::H64U(*VtxA, *VtxB);
			if (DualEdgeHashes.Contains(DualHash))
			{
				continue;
			}

			DualEdgeHashes.Add(DualHash);

			// The shared node is HE.TargetNode (= NextHE.OriginNode), get its point index
			const int32 SharedPointIdx = (*Cluster->Nodes)[HE.TargetNode].PointIndex;
			DualEdgeToSharedPointIdx.Add(DualHash, SharedPointIdx);
		}

		if (DualEdgeHashes.IsEmpty())
		{
			bIsProcessorValid = false;
			return true;
		}

		// Create output
		TSharedPtr<PCGExData::FPointIO> DualVtxIO = Context->MainPoints->Emplace_GetRef(PCGExData::EIOInit::New);
		if (!DualVtxIO->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New))
		{
			bIsProcessorValid = false;
			return true;
		}

		Context->VtxCarryOverDetails.Prune(DualVtxIO->Tags.Get());
		DualVtxIO->IOIndex = BatchIndex;
		PCGExClusters::Helpers::CleanupClusterData(DualVtxIO);

		UPCGBasePointData* OutputPoints = DualVtxIO->GetOut();
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutputPoints, NumValidEdges);

		DualVtxFacade = MakeShared<PCGExData::FFacade>(DualVtxIO.ToSharedRef());

		// Set up vertex blending (original edges → dual vertices)
		const bool bHasVtxBlending = Settings->VtxBlendingDetails.HasAnyBlending();
		if (bHasVtxBlending)
		{
			VtxBlender = MakeShared<PCGExBlending::FUnionBlender>(
				const_cast<FPCGExBlendingDetails*>(&Settings->VtxBlendingDetails),
				&Context->VtxCarryOverDetails,
				PCGExMath::GetNoneDistances());

			TArray<TSharedRef<PCGExData::FFacade>> BlendSources;
			BlendSources.Add(EdgeDataFacade);
			VtxBlender->AddSources(BlendSources, &PCGExClusters::Labels::ProtectedClusterAttributes);

			if (!VtxBlender->Init(Context, DualVtxFacade))
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Failed to initialize vertex blender for dual graph."));
				VtxBlender.Reset();
			}
		}

		// Write dual vertex positions and blend attributes
		TPCGValueRange<FTransform> OutTransforms = OutputPoints->GetTransformValueRange(true);
		TPCGValueRange<int32> OutSeeds = OutputPoints->GetSeedValueRange(true);

		TArray<PCGExData::FWeightedPoint> WeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;
		if (VtxBlender)
		{
			VtxBlender->InitTrackers(Trackers);
		}

		const TSharedPtr<PCGExSampling::FSampingUnionData> Union = MakeShared<PCGExSampling::FSampingUnionData>();
		const int32 EdgeSourceIOIndex = EdgeDataFacade->Source->IOIndex;

		int32 DualIdx = 0;
		int32 EdgeIdx = 0;
		for (const PCGExGraphs::FEdge& Edge : ClusterEdges)
		{
			if (!Edge.bValid)
			{
				++EdgeIdx;
				continue;
			}

			const FVector Midpoint = (Cluster->VtxTransforms[Edge.Start].GetLocation() +
				Cluster->VtxTransforms[Edge.End].GetLocation()) * 0.5;

			OutTransforms[DualIdx].SetLocation(Midpoint);
			OutSeeds[DualIdx] = PCGExRandomHelpers::ComputeSpatialSeed(Midpoint);

			// Blend attributes from original edge
			if (VtxBlender)
			{
				Union->Reset();
				Union->Reserve(1, 1);
				Union->AddWeighted_Unsafe(PCGExData::FElement(EdgeIdx, EdgeSourceIOIndex), 1.0);
				VtxBlender->ComputeWeights(DualIdx, Union, WeightedPoints);
				VtxBlender->Blend(DualIdx, WeightedPoints, Trackers);
			}

			++DualIdx;
			++EdgeIdx;
		}

		// Build graph with edge blending callback
		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(DualVtxFacade.ToSharedRef(), &Settings->GraphBuilderDetails);
		GraphBuilder->Graph->InsertEdges(DualEdgeHashes, BatchIndex);
		GraphBuilder->bInheritNodeData = false;
		GraphBuilder->EdgesIO = Context->MainEdges;

		// Set up vertex→edge blending via subgraph callbacks
		const bool bHasEdgeBlending = Settings->EdgeBlendingDetails.HasAnyBlending();
		if (bHasEdgeBlending)
		{
			GraphBuilder->OnPreCompile = [PCGEX_ASYNC_THIS_CAPTURE](PCGExGraphs::FSubGraphUserContext& UserContext, const PCGExGraphs::FSubGraphPreCompileData& Data)
			{
				PCGEX_ASYNC_THIS

				// Build mapping from output edge index to shared point index
				TArray<int32>& EdgeToSharedPoint = static_cast<FEdgeBlendContext&>(UserContext).EdgeToSharedPoint;
				EdgeToSharedPoint.SetNumUninitialized(Data.NumEdges);

				for (int32 i = 0; i < Data.NumEdges; ++i)
				{
					const PCGExGraphs::FEdge& E = Data.FlattenedEdges[i];
					const uint64 Hash = PCGEx::H64U(E.Start, E.End);
					const int32* SharedPointIdx = This->DualEdgeToSharedPointIdx.Find(Hash);
					EdgeToSharedPoint[i] = SharedPointIdx ? *SharedPointIdx : -1;
				}

				TSharedPtr<PCGExBlending::FUnionBlender>& EdgeBlender = static_cast<FEdgeBlendContext&>(UserContext).EdgeBlender;
				EdgeBlender = MakeShared<PCGExBlending::FUnionBlender>(
					const_cast<FPCGExBlendingDetails*>(&This->Settings->EdgeBlendingDetails),
					&This->Context->EdgeCarryOverDetails,
					PCGExMath::GetNoneDistances());

				TArray<TSharedRef<PCGExData::FFacade>> BlendSources;
				BlendSources.Add(This->VtxDataFacade);
				EdgeBlender->AddSources(BlendSources, &PCGExClusters::Labels::ProtectedClusterAttributes);

				if (!EdgeBlender->Init(This->Context, Data.EdgesDataFacade))
				{
					PCGE_LOG_C(Warning, GraphAndLog, This->Context, FTEXT("Failed to initialize edge blender for dual graph."));
					EdgeBlender.Reset();
				}
			};

			GraphBuilder->OnPostCompile = [PCGEX_ASYNC_THIS_CAPTURE](PCGExGraphs::FSubGraphUserContext& UserContext, const TSharedRef<PCGExGraphs::FSubGraph>& SubGraph)
			{
				PCGEX_ASYNC_THIS

				FEdgeBlendContext& Ctx = static_cast<FEdgeBlendContext&>(UserContext);
				if (!Ctx.EdgeBlender)
				{
					return;
				}

				const TArray<int32>& EdgeToSharedPoint = Ctx.EdgeToSharedPoint;
				const int32 NumEdges = SubGraph->FlattenedEdges.Num();
				const int32 VtxSourceIOIndex = This->VtxDataFacade->Source->IOIndex;

				TArray<PCGExData::FWeightedPoint> WPoints;
				TArray<PCGEx::FOpStats> Trk;
				Ctx.EdgeBlender->InitTrackers(Trk);

				const TSharedPtr<PCGExSampling::FSampingUnionData> U = MakeShared<PCGExSampling::FSampingUnionData>();

				for (int32 i = 0; i < NumEdges; ++i)
				{
					const int32 SharedPoint = EdgeToSharedPoint[i];
					if (SharedPoint < 0)
					{
						continue;
					}

					U->Reset();
					U->Reserve(1, 1);
					U->AddWeighted_Unsafe(PCGExData::FElement(SharedPoint, VtxSourceIOIndex), 1.0);
					Ctx.EdgeBlender->ComputeWeights(i, U, WPoints);
					Ctx.EdgeBlender->Blend(i, WPoints, Trk);
				}
			};

			GraphBuilder->OnCreateContext = []() -> TSharedPtr<PCGExGraphs::FSubGraphUserContext>
			{
				return MakeShared<FEdgeBlendContext>();
			};
		}

		GraphBuilder->CompileAsync(TaskManager, false, nullptr);

		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
	}

	void FProcessor::OnRangeProcessingComplete()
	{
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
