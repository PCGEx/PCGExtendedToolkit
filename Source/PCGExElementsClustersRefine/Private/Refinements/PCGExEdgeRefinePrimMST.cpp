// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefinePrimMST.h"

#pragma region FPCGExEdgeRefinePrimMST

void FPCGExEdgeRefinePrimMST::Process()
{
	const int32 NumNodes = Cluster->Nodes->Num();

	TBitArray<> Visited;
	Visited.Init(false, NumNodes);

	const PCGExClusters::FNode& RoamingSeedNode = *Heuristics->GetRoamingSeed();
	const PCGExClusters::FNode& RoamingGoalNode = *Heuristics->GetRoamingGoal();

	const TUniquePtr<PCGEx::FScoredQueue> ScoredQueue = MakeUnique<PCGEx::FScoredQueue>(NumNodes);
	ScoredQueue->Enqueue(RoamingSeedNode.Index, 0);
	const TSharedPtr<PCGEx::FHashLookup> TravelStack = PCGEx::NewHashLookup<PCGEx::FHashLookupArray>(PCGEx::NH64(-1, -1), NumNodes);

	int32 CurrentNodeIndex;
	double CurrentNodeScore;
	while (ScoredQueue->Dequeue(CurrentNodeIndex, CurrentNodeScore))
	{
		const PCGExClusters::FNode& Current = *Cluster->GetNode(CurrentNodeIndex);
		Visited[CurrentNodeIndex] = true;

		for (const PCGExGraphs::FLink Lk : Current.Links)
		{
			const uint32 NeighborIndex = Lk.Node;
			const uint32 EdgeIndex = Lk.Edge;

			if (Visited[NeighborIndex])
			{
				continue;
			} // Exit early

			const PCGExClusters::FNode& AdjacentNode = *Cluster->GetNode(NeighborIndex);
			PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(EdgeIndex);

			const double Score = Heuristics->GetEdgeScore(Current, AdjacentNode, Edge, RoamingSeedNode, RoamingGoalNode, nullptr, TravelStack.Get());
			if (!ScoredQueue->Enqueue(NeighborIndex, Score))
			{
				continue;
			}

			TravelStack->Set(NeighborIndex, PCGEx::NH64(CurrentNodeIndex, EdgeIndex));
		}
	}

	for (int32 i = 0; i < NumNodes; i++)
	{
		int32 Node;
		int32 EdgeIndex;

		PCGEx::NH64(TravelStack->Get(i), Node, EdgeIndex);
		if (Node == -1 || EdgeIndex == -1)
		{
			continue;
		}

		Cluster->GetEdge(EdgeIndex)->bValid = !bInvert;
	}
}

#pragma endregion
