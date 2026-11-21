// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPathQuery.h"

#include "PCGExHeuristicsHandler.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Search/PCGExSearchOperation.h"

namespace PCGExPathfinding
{
	FPathQuery::FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const FNodePick& InSeed, const FNodePick& InGoal, const int32 InQueryIndex)
		: Cluster(InCluster)
		  , Seed(InSeed)
		  , Goal(InGoal)
		  , QueryIndex(InQueryIndex)
	{
	}

	FPathQuery::FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const PCGExData::FConstPoint& InSeed, const PCGExData::FConstPoint& InGoal, const int32 InQueryIndex)
		: Cluster(InCluster)
		  , Seed(InSeed)
		  , Goal(InGoal)
		  , QueryIndex(InQueryIndex)
	{
	}

	FPathQuery::FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedPtr<FPathQuery>& PreviousQuery, const PCGExData::FConstPoint& InGoalPointRef, const int32 InQueryIndex)
		: Cluster(InCluster)
		  , Seed(PreviousQuery->Goal)
		  , Goal(InGoalPointRef)
		  , QueryIndex(InQueryIndex)
	{
	}

	FPathQuery::FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedPtr<FPathQuery>& PreviousQuery, const TSharedPtr<FPathQuery>& NextQuery, const int32 InQueryIndex)
		: Cluster(InCluster)
		  , Seed(PreviousQuery->Goal)
		  , Goal(NextQuery->Seed)
		  , QueryIndex(InQueryIndex)
	{
	}

	EQueryPickResolution FPathQuery::ResolvePicks(const FPCGExNodeSelectionDetails& SeedSelectionDetails, const FPCGExNodeSelectionDetails& GoalSelectionDetails)
	{
		// Both are always attempted; a pick is resolved exactly when it holds a node.
		Seed.ResolveNode(Cluster, SeedSelectionDetails);
		Goal.ResolveNode(Cluster, GoalSelectionDetails);

		return UpdatePickResolution();
	}

	EQueryPickResolution FPathQuery::UpdatePickResolution()
	{
		if (!Seed.IsValid())
		{
			PickResolution = Goal.IsValid() ? EQueryPickResolution::UnresolvedSeed : EQueryPickResolution::UnresolvedPicks;
		}
		else if (!Goal.IsValid())
		{
			PickResolution = EQueryPickResolution::UnresolvedGoal;
		}
		else if (Seed.Node == Goal.Node)
		{
			PickResolution = EQueryPickResolution::SameSeedAndGoal;
		}
		else
		{
			PickResolution = EQueryPickResolution::Success;
		}

		return PickResolution;
	}

	void FPathQuery::AddPathNode(const int32 InNodeIndex, const int32 InEdgeIndex)
	{
		PathNodes.Add(InNodeIndex);
		if (InEdgeIndex != -1)
		{
			PathEdges.Add(InEdgeIndex);
		}
	}

	void FPathQuery::SetResolution(const EPathfindingResolution InResolution)
	{
		Resolution = InResolution;

		if (Resolution == EPathfindingResolution::Success)
		{
			Algo::Reverse(PathNodes);
			Algo::Reverse(PathEdges);
		}
	}

	void FPathQuery::FindPath(const TSharedPtr<FPCGExSearchOperation>& SearchOperation, const TSharedPtr<FSearchAllocations>& Allocations, const TSharedPtr<PCGExHeuristics::FHandler>& HeuristicsHandler, const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback)
	{
		if (PickResolution != EQueryPickResolution::Success)
		{
			SetResolution(EPathfindingResolution::Fail);
			return;
		}

		PCGEX_SHARED_THIS_DECL

		if (SearchOperation->ResolveQuery(ThisPtr, Allocations, HeuristicsHandler, LocalFeedback))
		{
			SetResolution(HasValidPathPoints() ? EPathfindingResolution::Success : EPathfindingResolution::Fail);
		}
		else
		{
			SetResolution(EPathfindingResolution::Fail);
		}

		if (Resolution == EPathfindingResolution::Fail)
		{
			return;
		}

		const TArray<PCGExClusters::FNode>& NodesRef = *Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& EdgesRef = *Cluster->Edges;

		// Feedback scores

		if (!HeuristicsHandler->HasAnyFeedback())
		{
			return;
		}

		if (HeuristicsHandler->HasGlobalFeedback() && LocalFeedback)
		{
			for (int i = 0; i < PathEdges.Num(); i++)
			{
				const PCGExClusters::FNode& Node = NodesRef[PathNodes[i]];
				const PCGExGraphs::FEdge& Edge = EdgesRef[PathEdges[i]];
				HeuristicsHandler->FeedbackScore(Node, Edge);
				LocalFeedback->FeedbackScore(Node, Edge);
			}

			HeuristicsHandler->FeedbackPointScore(NodesRef[PathNodes.Last()]);
			LocalFeedback->FeedbackPointScore(NodesRef[PathNodes.Last()]);
		}
		else if (LocalFeedback)
		{
			for (int i = 0; i < PathEdges.Num(); i++)
			{
				LocalFeedback->FeedbackScore(NodesRef[PathNodes[i]], EdgesRef[PathEdges[i]]);
			}
			LocalFeedback->FeedbackPointScore(NodesRef[PathNodes.Last()]);
		}
		else
		{
			for (int i = 0; i < PathEdges.Num(); i++)
			{
				HeuristicsHandler->FeedbackScore(NodesRef[PathNodes[i]], EdgesRef[PathEdges[i]]);
			}
			HeuristicsHandler->FeedbackPointScore(NodesRef[PathNodes.Last()]);
		}
	}

	void FPathQuery::AppendNodePoints(TArray<int32>& OutPoints, const int32 TruncateStart, const int32 TruncateEnd) const
	{
		const int32 Count = PathNodes.Num() - TruncateEnd;
		for (int i = TruncateStart; i < Count; i++)
		{
			OutPoints.Add(Cluster->GetNodePointIndex(PathNodes[i]));
		}
	}

	void FPathQuery::AppendEdgePoints(TArray<int32>& OutPoints) const
	{
		OutPoints.Reserve(OutPoints.Num() + PathEdges.Num());
		OutPoints.Append(PathEdges);
	}

	void FPathQuery::Cleanup()
	{
		PathNodes.Empty();
		PathEdges.Empty();
	}
}
