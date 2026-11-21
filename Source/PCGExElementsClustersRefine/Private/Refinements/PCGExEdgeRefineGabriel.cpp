// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineGabriel.h"

#pragma region FPCGExEdgeRefineGabriel

void FPCGExEdgeRefineGabriel::PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics)
{
	FPCGExEdgeRefineOperation::PrepareForCluster(InCluster, InHeuristics);
	ExchangeValue = bInvert ? 1 : 0;
}

void FPCGExEdgeRefineGabriel::ProcessEdge(PCGExGraphs::FEdge& Edge)
{
	FPCGExEdgeRefineOperation::ProcessEdge(Edge);

	const FVector From = Cluster->GetStartPos(Edge);
	const FVector To = Cluster->GetEndPos(Edge);

	const FVector Center = FMath::Lerp(From, To, 0.5);
	const double SqrDist = FVector::DistSquared(Center, From);

	Cluster->NodeOctree->FindFirstElementWithBoundsTest(FBoxCenterAndExtent(Center, FVector(FMath::Sqrt(SqrDist))), [&](const PCGExOctree::FItem& Item)
	{
		if (FVector::DistSquared(Center, Cluster->GetPos(Item.Index)) < SqrDist)
		{
			FPlatformAtomics::InterlockedExchange(&Edge.bValid, ExchangeValue);
			return false;
		}
		return true;
	});
}

#pragma endregion
