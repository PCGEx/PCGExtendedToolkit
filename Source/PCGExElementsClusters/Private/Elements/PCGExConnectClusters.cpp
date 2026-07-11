// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExConnectClusters.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Math/Geo/PCGExDelaunay.h"
#include "Utils/PCGExPointIOMerger.h"
#include "Graphs/PCGExGraphPatcher.h"

#define LOCTEXT_NAMESPACE "PCGExConnectClusters"
#define PCGEX_NAMESPACE ConnectClusters

PCGExData::EIOInit UPCGExConnectClustersSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExConnectClustersSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(ConnectClusters)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(ConnectClusters)

bool FPCGExConnectClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ConnectClusters)

	PCGEX_FWD(CarryOverDetails)
	Context->CarryOverDetails.Init();

	PCGEX_FWD(ProjectionDetails)

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

bool FPCGExConnectClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExConnectClustersElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ConnectClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[&](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				if (Entries->Entries.Num() == 1)
				{
					// No clusters to consolidate, just dump existing points
					Context->CurrentIO->InitializeOutput(PCGExData::EIOInit::Forward);
					Entries->Entries[0]->InitializeOutput(PCGExData::EIOInit::Forward);
					return false;
				}

				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			if (!Settings->bQuietNoBridgeWarning)
			{
				PCGE_LOG(Warning, GraphAndLog, FTEXT("No bridge was created."));
			}

			for (const TSharedPtr<PCGExData::FPointIO>& Vtx : Context->MainPoints->Pairs)
			{
				Vtx->InitializeOutput(PCGExData::EIOInit::Forward);
			}
			for (const TSharedPtr<PCGExData::FPointIO>& Edges : Context->MainEdges->Pairs)
			{
				Edges->InitializeOutput(PCGExData::EIOInit::Forward);
			}

			Context->OutputPointsAndEdges();
			return Context->TryComplete(true);
		}
	}


	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	// The patcher already paired each component's edges with the vtx during processing.
	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

namespace PCGExConnectClusters
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExConnectClusters::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Cluster->GetNodeOctree();

		return true;
	}

	void FProcessor::CompleteWork()
	{
	}

	//////// BATCH

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
		InVtx->InitializeOutput(PCGExData::EIOInit::Duplicate);
	}

	void FBatch::CompleteWork()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectClusters)

		const int32 NumValidClusters = GatherValidClusters();

		if (Processors.Num() != NumValidClusters)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some vtx/edges groups have invalid clusters. Make sure to sanitize the input first."));
		}

		if (ValidClusters.IsEmpty())
		{
			return;
		} // Skip work completion entirely

		// Register every valid cluster's edge group with the patcher (group index == ValidClusters index).
		Patcher = MakeShared<PCGExGraphs::FGraphPatcher>(VtxDataFacade);
		for (const TSharedPtr<PCGExClusters::FCluster>& Cl : ValidClusters)
		{
			TArray<int32> VtxIndices;
			VtxIndices.Reserve(Cl->Nodes->Num());
			for (const PCGExClusters::FNode& Node : *Cl->Nodes) { VtxIndices.Add(Node.PointIndex); }
			Patcher->AddEdgeGroup(Cl->EdgesIO.Pin(), VtxIndices);
		}

		const int32 NumBounds = ValidClusters.Num();
		EPCGExBridgeClusterMethod SafeMethod = Settings->BridgeMethod;

		// Too few sites for a triangulation: fall back to MostEdges. These are independent checks --
		// folding them into an if/else-if chain on NumBounds shadows the 2D case (every <=3 is also <=4).
		if (NumBounds <= 4 && SafeMethod == EPCGExBridgeClusterMethod::Delaunay3D)
		{
			SafeMethod = EPCGExBridgeClusterMethod::MostEdges;
		}
		if (NumBounds <= 3 && SafeMethod == EPCGExBridgeClusterMethod::Delaunay2D)
		{
			SafeMethod = EPCGExBridgeClusterMethod::MostEdges;
		}

		// First find which cluster are connected

		TArray<FBox> Bounds;
		PCGExArrayHelpers::InitArray(Bounds, NumBounds);
		for (int i = 0; i < NumBounds; i++)
		{
			Bounds[i] = ValidClusters[i]->Bounds;
		}

		if (SafeMethod == EPCGExBridgeClusterMethod::Delaunay3D)
		{
			const TUniquePtr<PCGExMath::Geo::TDelaunay3> Delaunay = MakeUnique<PCGExMath::Geo::TDelaunay3>();

			TArray<FVector> Positions;
			Positions.SetNum(NumBounds);

			for (int i = 0; i < NumBounds; i++)
			{
				Positions[i] = Bounds[i].GetCenter();
			}

			if (Delaunay->Process<false, false>(Positions))
			{
				Bridges.Append(Delaunay->DelaunayEdges);
			}
			else
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Delaunay 3D failed. Are points coplanar? If so, use Delaunay 2D instead."));
			}

			Positions.Empty();
		}
		else if (SafeMethod == EPCGExBridgeClusterMethod::Delaunay2D)
		{
			const TUniquePtr<PCGExMath::Geo::TDelaunay2> Delaunay = MakeUnique<PCGExMath::Geo::TDelaunay2>();

			TArray<FVector> Positions;
			Positions.SetNum(NumBounds);

			for (int i = 0; i < NumBounds; i++)
			{
				Positions[i] = Bounds[i].GetCenter();
			}

			// Only DelaunayEdges are consumed; skip hull extraction.
			if (Delaunay->Process(Positions, Context->ProjectionDetails, true, false))
			{
				Bridges.Append(Delaunay->DelaunayEdges);
			}
			else
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Delaunay 2D failed."));
			}

			Positions.Empty();
		}
		else if (SafeMethod == EPCGExBridgeClusterMethod::LeastEdges)
		{
			// Minimum spanning tree (Prim's, O(NumBounds^2)) so the clusters are connected with the
			// globally-shortest set of bridges. Weight = distance between cluster bounds centers.
			if (NumBounds > 1)
			{
				TArray<bool> InTree;
				InTree.Init(false, NumBounds);
				TArray<double> BestDist;
				BestDist.Init(TNumericLimits<double>::Max(), NumBounds);
				TArray<int32> BestFrom;
				BestFrom.Init(-1, NumBounds);

				// Seed the tree with cluster 0, then grow it one nearest cluster at a time.
				InTree[0] = true;
				for (int j = 1; j < NumBounds; j++)
				{
					BestDist[j] = FVector::DistSquared(Bounds[0].GetCenter(), Bounds[j].GetCenter());
					BestFrom[j] = 0;
				}

				for (int Added = 1; Added < NumBounds; Added++)
				{
					int32 Next = -1;
					double NextDist = TNumericLimits<double>::Max();
					for (int j = 0; j < NumBounds; j++)
					{
						if (!InTree[j] && BestDist[j] < NextDist)
						{
							NextDist = BestDist[j];
							Next = j;
						}
					}
					if (Next == -1) { break; } // disconnected guard (shouldn't happen with finite distances)

					InTree[Next] = true;
					Bridges.Add(PCGEx::H64(BestFrom[Next], Next));

					// Relax remaining clusters against the one just added.
					for (int j = 0; j < NumBounds; j++)
					{
						if (InTree[j]) { continue; }
						if (const double Dist = FVector::DistSquared(Bounds[Next].GetCenter(), Bounds[j].GetCenter());
							Dist < BestDist[j])
						{
							BestDist[j] = Dist;
							BestFrom[j] = Next;
						}
					}
				}
			}
		}
		else if (SafeMethod == EPCGExBridgeClusterMethod::MostEdges)
		{
			for (int i = 0; i < NumBounds; i++)
			{
				for (int j = 0; j < NumBounds; j++)
				{
					if (i == j)
					{
						continue;
					}
					Bridges.Add(PCGEx::H64U(i, j));
				}
			}
		}

		// Resolve each connected cluster pair to its closest vtx pair and stage a bridge edge.
		BridgesList = Bridges.Array();
		BridgeEdgeHandles.Reset(BridgesList.Num());
		BridgeEndpoints.Reset(BridgesList.Num());
		for (const uint64 BridgeHash : BridgesList)
		{
			int32 VtxA = -1;
			int32 VtxB = -1;
			if (!FindClosestVtxPair(PCGEx::H64A(BridgeHash), PCGEx::H64B(BridgeHash), VtxA, VtxB)) { continue; }
			BridgeEdgeHandles.Add(Patcher->AddEdge(VtxA, VtxB));
			BridgeEndpoints.Add(PCGEx::H64(VtxA, VtxB));
		}

		// Components -> merged edge IOs (async; ready by Write). ConnectClusters links existing vtx
		// directly, so there are no new vtx -- only edges.
		Patcher->ResolveAndMergeAsync(Context->MainEdges.ToSharedRef(), TaskManager, &Context->CarryOverDetails);
	}

	void FBatch::Write()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(ConnectClusters)

		if (!Patcher) { return; }

		// Append + patch the staged bridge edges into their component edge collections.
		Patcher->Commit();

		// Optional connector flags (node-specific; the patcher handles topology only).
		PCGExGraphs::WriteConnectorFlags(
			*Patcher, VtxDataFacade,
			{Settings->bFlagVtxConnector, Settings->VtxConnectorFlagName, Settings->bFlagEdgeConnector, Settings->EdgeConnectorFlagName},
			BridgeEdgeHandles, BridgeEndpoints);
	}


	bool FBatch::FindClosestVtxPair(const int32 FromClusterIndex, const int32 ToClusterIndex, int32& OutVtxA, int32& OutVtxB) const
	{
		const TSharedPtr<PCGExClusters::FCluster> ClusterA = ValidClusters[FromClusterIndex];
		const TSharedPtr<PCGExClusters::FCluster> ClusterB = ValidClusters[ToClusterIndex];

		OutVtxA = -1;
		OutVtxB = -1;
		double Distance = TNumericLimits<double>::Max();

		const TArray<PCGExClusters::FNode>& NodesRefA = *ClusterA->Nodes;
		const TArray<PCGExClusters::FNode>& NodesRefB = *ClusterB->Nodes;

		auto Consider = [&](const PCGExClusters::FNode& NodeA, const FVector& PosA, const PCGExClusters::FNode& NodeB)
		{
			if (const double Dist = FVector::DistSquared(PosA, ClusterB->GetPos(NodeB));
				Dist < Distance)
			{
				OutVtxA = NodeA.PointIndex;
				OutVtxB = NodeB.PointIndex;
				Distance = Dist;
			}
		};

		for (const PCGExClusters::FNode& NodeA : NodesRefA)
		{
			const FVector PosA = ClusterA->GetPos(NodeA);

			// FindClosestNode is a bounded octree query: it returns INDEX_NONE when PosA lies outside B's
			// populated cells (clusters far apart). On a miss, brute-force B -- never index with INDEX_NONE.
			const int32 ClosestBIdx = ClusterB->FindClosestNode(PosA);
			if (NodesRefB.IsValidIndex(ClosestBIdx))
			{
				Consider(NodeA, PosA, NodesRefB[ClosestBIdx]);
			}
			else
			{
				for (const PCGExClusters::FNode& NodeB : NodesRefB) { Consider(NodeA, PosA, NodeB); }
			}
		}

		return OutVtxA >= 0 && OutVtxB >= 0;
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
