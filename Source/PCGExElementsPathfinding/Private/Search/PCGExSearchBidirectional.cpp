// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Search/PCGExSearchBidirectional.h"

#include "PCGExHeuristicsHandler.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Core/PCGExPathQuery.h"
#include "Core/PCGExPathfinding.h"
#include "Core/PCGExSearchAllocations.h"
#include "Utils/PCGExScoredQueue.h"

namespace PCGExPathfinding
{
	void FBidirectionalSearchAllocations::Init(const PCGExClusters::FCluster* InCluster)
	{
		FSearchAllocations::Init(InCluster);

		InitGScore(-1);
		VisitedBackward.Init(false, NumNodes);
		GScoreBackward.Init(-1, NumNodes);
		TravelStackBackward = MakeShared<PCGEx::FHashLookupArray>(PCGEx::NH64(-1, -1), NumNodes);
		ScoredQueueBackward = MakeShared<PCGEx::FScoredQueue>(NumNodes);
	}

	void FBidirectionalSearchAllocations::Reset()
	{
		FSearchAllocations::Reset();
		ResetSearchState(ScoredQueueBackward, VisitedBackward, GScoreBackward, -1, TravelStackBackward);
	}
}

bool FPCGExSearchOperationBidirectional::ResolveQuery(
	const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
	const TSharedPtr<PCGExPathfinding::FSearchAllocations>& Allocations,
	const TSharedPtr<PCGExHeuristics::FHandler>& Heuristics,
	const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback) const
{
	check(InQuery->PickResolution == PCGExPathfinding::EQueryPickResolution::Success)

	TSharedPtr<PCGExPathfinding::FBidirectionalSearchAllocations> LocalAllocations;
	if (Allocations)
	{
		LocalAllocations = StaticCastSharedPtr<PCGExPathfinding::FBidirectionalSearchAllocations>(Allocations);
		LocalAllocations->Reset();
	}
	else
	{
		LocalAllocations = StaticCastSharedPtr<PCGExPathfinding::FBidirectionalSearchAllocations>(NewAllocations());
	}

	const TArray<PCGExClusters::FNode>& NodesRef = *Cluster->Nodes;
	const TArray<PCGExGraphs::FEdge>& EdgesRef = *Cluster->Edges;

	const PCGExClusters::FNode& SeedNode = *InQuery->Seed.Node;
	const PCGExClusters::FNode& GoalNode = *InQuery->Goal.Node;

	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSearchOperationBidirectional::FindPath);

	// Forward search structures
	TBitArray<>& VisitedForward = LocalAllocations->Visited;
	TArray<double>& GScoreForward = LocalAllocations->GScore;
	PCGEx::FHashLookupArray* TravelStackForward = LocalAllocations->TravelStack.Get();
	uint64* const TravelDataForward = TravelStackForward->GetMutableData();
	PCGEx::FScoredQueue* QueueForward = LocalAllocations->ScoredQueue.Get();

	// Backward search structures
	TBitArray<>& VisitedBackward = LocalAllocations->VisitedBackward;
	TArray<double>& GScoreBackward = LocalAllocations->GScoreBackward;
	PCGEx::FHashLookupArray* TravelStackBackward = LocalAllocations->TravelStackBackward.Get();
	uint64* const TravelDataBackward = TravelStackBackward->GetMutableData();
	PCGEx::FScoredQueue* QueueBackward = LocalAllocations->ScoredQueueBackward.Get();

	// Initialize forward search from seed
	QueueForward->Enqueue(SeedNode.Index, 0);
	GScoreForward[SeedNode.Index] = 0;

	// Initialize backward search from goal
	QueueBackward->Enqueue(GoalNode.Index, 0);
	GScoreBackward[GoalNode.Index] = 0;

	const PCGExHeuristics::FLocalFeedbackHandler* Feedback = LocalFeedback.Get();

	int32 MeetingNode = -1;
	double BestPathCost = TNumericLimits<double>::Max();

	// Alternate between forward and backward searches
	while (!QueueForward->IsEmpty() || !QueueBackward->IsEmpty())
	{
		// Forward step
		if (!QueueForward->IsEmpty())
		{
			int32 CurrentNodeIndex;
			double CurrentScore;
			QueueForward->Dequeue(CurrentNodeIndex, CurrentScore);

			// Check if we've found a better path
			if (CurrentScore >= BestPathCost)
			{
				continue;
			}

			// Check if backward search has reached this node
			if (VisitedBackward[CurrentNodeIndex])
			{
				const double PathCost = GScoreForward[CurrentNodeIndex] + GScoreBackward[CurrentNodeIndex];
				if (PathCost < BestPathCost)
				{
					BestPathCost = PathCost;
					MeetingNode = CurrentNodeIndex;
				}
			}

			if (!VisitedForward[CurrentNodeIndex])
			{
				VisitedForward[CurrentNodeIndex] = true;
				const PCGExClusters::FNode& Current = NodesRef[CurrentNodeIndex];
				const double CurrentGScore = GScoreForward[CurrentNodeIndex];

				for (const PCGExGraphs::FLink Lk : Current.Links)
				{
					const uint32 NeighborIndex = Lk.Node;
					const uint32 EdgeIndex = Lk.Edge;

					if (VisitedForward[NeighborIndex])
					{
						continue;
					}

					const PCGExClusters::FNode& AdjacentNode = NodesRef[NeighborIndex];
					const PCGExGraphs::FEdge& Edge = EdgesRef[EdgeIndex];

					const double EScore = Heuristics->GetEdgeScore(Current, AdjacentNode, Edge, SeedNode, GoalNode, Feedback, TravelStackForward);
					const double TentativeGScore = CurrentGScore + EScore;

					const double PreviousGScore = GScoreForward[NeighborIndex];
					if (PreviousGScore != -1 && TentativeGScore >= PreviousGScore)
					{
						continue;
					}

					TravelDataForward[NeighborIndex] = PCGEx::NH64(CurrentNodeIndex, EdgeIndex);
					GScoreForward[NeighborIndex] = TentativeGScore;

					QueueForward->Enqueue(NeighborIndex, TentativeGScore);
				}
			}
		}

		// Backward step
		if (!QueueBackward->IsEmpty())
		{
			int32 CurrentNodeIndex;
			double CurrentScore;
			QueueBackward->Dequeue(CurrentNodeIndex, CurrentScore);

			if (CurrentScore >= BestPathCost)
			{
				continue;
			}

			// Check if forward search has reached this node
			if (VisitedForward[CurrentNodeIndex])
			{
				const double PathCost = GScoreForward[CurrentNodeIndex] + GScoreBackward[CurrentNodeIndex];
				if (PathCost < BestPathCost)
				{
					BestPathCost = PathCost;
					MeetingNode = CurrentNodeIndex;
				}
			}

			if (!VisitedBackward[CurrentNodeIndex])
			{
				VisitedBackward[CurrentNodeIndex] = true;
				const PCGExClusters::FNode& Current = NodesRef[CurrentNodeIndex];
				const double CurrentGScore = GScoreBackward[CurrentNodeIndex];

				for (const PCGExGraphs::FLink Lk : Current.Links)
				{
					const uint32 NeighborIndex = Lk.Node;
					const uint32 EdgeIndex = Lk.Edge;

					if (VisitedBackward[NeighborIndex])
					{
						continue;
					}

					const PCGExClusters::FNode& AdjacentNode = NodesRef[NeighborIndex];
					const PCGExGraphs::FEdge& Edge = EdgesRef[EdgeIndex];

					// Note: For backward search, we reverse the direction conceptually
					const double EScore = Heuristics->GetEdgeScore(Current, AdjacentNode, Edge, GoalNode, SeedNode, Feedback, TravelStackBackward);
					const double TentativeGScore = CurrentGScore + EScore;

					const double PreviousGScore = GScoreBackward[NeighborIndex];
					if (PreviousGScore != -1 && TentativeGScore >= PreviousGScore)
					{
						continue;
					}

					TravelDataBackward[NeighborIndex] = PCGEx::NH64(CurrentNodeIndex, EdgeIndex);
					GScoreBackward[NeighborIndex] = TentativeGScore;

					QueueBackward->Enqueue(NeighborIndex, TentativeGScore);
				}
			}
		}

		// Early termination check
		if (MeetingNode != -1 && QueueForward->IsEmpty() && QueueBackward->IsEmpty())
		{
			break;
		}
	}

	if (MeetingNode == -1)
	{
		return false;
	}

	// Reconstruct path
	ReconstructPath(InQuery, MeetingNode, TravelStackForward, TravelStackBackward, SeedNode.Index, GoalNode.Index);

	return true;
}

void FPCGExSearchOperationBidirectional::ReconstructPath(
	const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
	int32 MeetingNode,
	PCGEx::FHashLookup* ForwardStack,
	PCGEx::FHashLookup* BackwardStack,
	int32 SeedIndex,
	int32 GoalIndex) const
{
	// Build paths in goal-to-seed order so FPathQuery::SetResolution's reverse
	// produces the conventional seed-to-goal output (matching A*/Dijkstra/BellmanFord).

	// Goal-side: walk BackwardStack from Meeting toward Goal, then reverse to get goal-to-meeting.
	TArray<int32> BackwardPath;
	TArray<int32> BackwardEdges;

	int32 CurrentNode = MeetingNode;
	while (CurrentNode != GoalIndex)
	{
		int32 NextNode, EdgeIndex;
		PCGEx::NH64(BackwardStack->Get(CurrentNode), NextNode, EdgeIndex);
		if (NextNode == -1)
		{
			break;
		}
		BackwardEdges.Add(EdgeIndex);
		BackwardPath.Add(NextNode);
		CurrentNode = NextNode;
	}
	Algo::Reverse(BackwardPath);
	Algo::Reverse(BackwardEdges);

	// Seed-side: walk ForwardStack from Meeting back toward Seed via predecessors.
	// Already in meeting-to-seed order, which is what we want.
	TArray<int32> ForwardPath;
	TArray<int32> ForwardEdges;

	CurrentNode = MeetingNode;
	while (CurrentNode != SeedIndex)
	{
		ForwardPath.Add(CurrentNode);
		int32 PrevNode, EdgeIndex;
		PCGEx::NH64(ForwardStack->Get(CurrentNode), PrevNode, EdgeIndex);
		if (PrevNode == -1)
		{
			break;
		}
		ForwardEdges.Add(EdgeIndex);
		CurrentNode = PrevNode;
	}
	ForwardPath.Add(SeedIndex);

	// Merge: goal-side first (Goal..B), then seed-side (Meeting..Seed).
	for (int i = 0; i < BackwardPath.Num(); i++)
	{
		InQuery->AddPathNode(BackwardPath[i], i < BackwardEdges.Num() ? BackwardEdges[i] : -1);
	}

	for (int i = 0; i < ForwardPath.Num(); i++)
	{
		InQuery->AddPathNode(ForwardPath[i], i < ForwardEdges.Num() ? ForwardEdges[i] : -1);
	}
}

TSharedPtr<PCGExPathfinding::FSearchAllocations> FPCGExSearchOperationBidirectional::NewAllocations() const
{
	TSharedPtr<PCGExPathfinding::FBidirectionalSearchAllocations> Allocations = MakeShared<PCGExPathfinding::FBidirectionalSearchAllocations>();
	Allocations->Init(Cluster);
	return Allocations;
}
