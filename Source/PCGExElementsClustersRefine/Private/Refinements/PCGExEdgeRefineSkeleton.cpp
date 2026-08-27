// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineSkeleton.h"

#pragma region FPCGExEdgeRefineSkeleton

void FPCGExEdgeRefineSkeleton::PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics)
{
	FPCGExEdgeRefineOperation::PrepareForCluster(InCluster, InHeuristics);
	ExchangeValue = bInvert ? 1 : 0;
	// ClampMin only guards the panel; Beta is PCG_Overridable and divides the edge length.
	Beta = FMath::Max(Beta, 0.001);
}

void FPCGExEdgeRefineSkeleton::ProcessEdge(PCGExGraphs::FEdge& Edge)
{
	FPCGExEdgeRefineOperation::ProcessEdge(Edge);

	const FVector From = Cluster->GetStartPos(Edge);
	const FVector To = Cluster->GetEndPos(Edge);
	const FVector Center = FMath::Lerp(From, To, 0.5);
	const double Dist = FVector::Dist(From, To);

	// The lune and circle neighborhoods are defined over vtx other than the edge's own two.
	const int32 StartNode = Cluster->GetEdgeStart(Edge)->Index;
	const int32 EndNode = Cluster->GetEdgeEnd(Edge)->Index;

	if (Beta <= 1)
	{
		// Lune-based condition (Beta-Skeleton for 0 < Beta <= 1)
		const double SqrDist = FMath::Square(Dist / Beta);

		Cluster->NodeOctree->FindFirstElementWithBoundsTest(FBoxCenterAndExtent(Center, FVector(FMath::Sqrt(SqrDist) + 1)), [&](const PCGExOctree::FItem& Item)
		{
			if (Item.Index == StartNode || Item.Index == EndNode)
			{
				return true;
			}

			const FVector& OtherPoint = Cluster->GetPos(Item.Index);
			if (FVector::DistSquared(OtherPoint, From) < SqrDist && FVector::DistSquared(OtherPoint, To) < SqrDist)
			{
				FPlatformAtomics::InterlockedExchange(&Edge.bValid, ExchangeValue);
				return false;
			}
			return true;
		});
	}
	else
	{
		const FVector Normal = PCGExMath::GetNormalUp(From, To, FVector::UpVector) * (Dist * Beta);
		const double SqrDist = FMath::Square(Dist);

		const FVector C1 = Center + Normal;
		const FVector C2 = Center - Normal;

		Cluster->NodeOctree->FindFirstElementWithBoundsTest(FBoxCenterAndExtent(Center, FVector(FMath::Sqrt(SqrDist) + 1)), [&](const PCGExOctree::FItem& Item)
		{
			if (Item.Index == StartNode || Item.Index == EndNode)
			{
				return true;
			}

			const FVector& OtherPoint = Cluster->GetPos(Item.Index);
			if (FVector::DistSquared(OtherPoint, C1) < SqrDist || FVector::DistSquared(OtherPoint, C2) < SqrDist)
			{
				FPlatformAtomics::InterlockedExchange(&Edge.bValid, ExchangeValue);
				return false;
			}
			return true;
		});
	}
}

#pragma endregion

#pragma region UPCGExEdgeRefineSkeleton

void UPCGExEdgeRefineSkeleton::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExEdgeRefineSkeleton* TypedOther = Cast<UPCGExEdgeRefineSkeleton>(Other))
	{
		Beta = TypedOther->Beta;
		bInvert = TypedOther->bInvert;
	}
}

#pragma endregion
