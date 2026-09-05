// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineKeepLongest.h"

#pragma region FPCGExEdgeKeepLongest

void FPCGExEdgeKeepLongest::ProcessNode(PCGExClusters::FNode& Node)
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

	FPlatformAtomics::InterlockedExchange(&Cluster->GetEdge(BestIndex)->bValid, 1);
}

#pragma endregion
