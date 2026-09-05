// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineRemoveHighestScore.h"

#pragma region FPCGExEdgeRemoveHighestScore

void FPCGExEdgeRemoveHighestScore::ProcessNode(PCGExClusters::FNode& Node)
{
	int32 BestIndex = -1;
	double HighestScore = TNumericLimits<double>::Lowest();

	const PCGExClusters::FNode& RoamingSeedNode = *Heuristics->GetRoamingSeed();
	const PCGExClusters::FNode& RoamingGoalNode = *Heuristics->GetRoamingGoal();

	for (const PCGExGraphs::FLink Lk : Node.Links)
	{
		const double Score = Heuristics->GetEdgeScore(Node, *Cluster->GetNode(Lk), *Cluster->GetEdge(Lk), RoamingSeedNode, RoamingGoalNode);
		if (Score > HighestScore)
		{
			HighestScore = Score;
			BestIndex = Lk.Edge;
		}
	}

	if (BestIndex == -1)
	{
		return;
	}

	FPlatformAtomics::InterlockedExchange(&Cluster->GetEdge(BestIndex)->bValid, 0);
}

#pragma endregion
