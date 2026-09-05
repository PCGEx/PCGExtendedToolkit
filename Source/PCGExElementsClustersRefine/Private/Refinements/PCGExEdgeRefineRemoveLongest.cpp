// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineRemoveLongest.h"

#pragma region FPCGExEdgeRemoveLongest

void FPCGExEdgeRemoveLongest::ProcessNode(PCGExClusters::FNode& Node)
{
	int32 BestIndex = -1;
	double LongestDist = 0;

	for (const PCGExGraphs::FLink Lk : Node.Links)
	{
		const double Dist = Cluster->GetDistSquared(Node.Index, Lk.Node);
		if (Dist > LongestDist)
		{
			LongestDist = Dist;
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
