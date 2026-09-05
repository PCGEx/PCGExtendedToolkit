// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineRemoveOverlap.h"

#pragma region FPCGExEdgeRemoveOverlap

void FPCGExEdgeRemoveOverlap::PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics)
{
	FPCGExEdgeRefineOperation::PrepareForCluster(InCluster, InHeuristics);
	MinDot = bUseMinAngle ? PCGExMath::DegreesToDot(MinAngle) : 1;
	MaxDot = bUseMaxAngle ? PCGExMath::DegreesToDot(MaxAngle) : -1;
	ToleranceSquared = FMath::Square(Tolerance);
	Cluster->GetBoundedEdges(true); // Let's hope it was cached ^_^
}

void FPCGExEdgeRemoveOverlap::ProcessEdge(PCGExGraphs::FEdge& Edge)
{
	const double Length = Cluster->GetDistSquared(Edge);

	const FVector A1 = Cluster->GetStartPos(Edge);
	const FVector B1 = Cluster->GetEndPos(Edge);

	auto ProcessOverlap = [&](const PCGExOctree::FItem& Item)
	{

		const PCGExGraphs::FEdge& OtherEdge = *Cluster->GetEdge(Item.Index);

		if (Edge.Index == OtherEdge.Index || Edge.Start == OtherEdge.Start || Edge.Start == OtherEdge.End || Edge.End == OtherEdge.End || Edge.End == OtherEdge.Start)
		{
			return true;
		}


		if (bUseMinAngle || bUseMaxAngle)
		{
			const double Dot = FMath::Abs(FVector::DotProduct(Cluster->GetEdgeDir(Edge), Cluster->GetEdgeDir(OtherEdge)));
			if (!(Dot >= MaxDot && Dot <= MinDot))
			{
				return true;
			}
		}

		const double OtherLength = Cluster->GetDistSquared(OtherEdge);

		FVector A;
		FVector B;
		if (Cluster->EdgeDistToEdgeSquared(&Edge, &OtherEdge, A, B) >= ToleranceSquared)
		{
			return true;
		}

		const FVector A2 = Cluster->GetStartPos(OtherEdge);
		const FVector B2 = Cluster->GetEndPos(OtherEdge);

		if (A == A1 || A == B1 || A == A2 || A == B2 || B == A2 || B == B2 || B == A1 || B == B1)
		{
			return true;
		}

		if (Keep == EPCGExEdgeOverlapPick::Longest)
		{
			if (OtherLength > Length)
			{
				FPlatformAtomics::InterlockedExchange(&Edge.bValid, 0);
				return false;
			}
		}
		else
		{
			if (OtherLength < Length)
			{
				FPlatformAtomics::InterlockedExchange(&Edge.bValid, 0);
				return false;
			}
		}

		return true;
	};

	(void)Cluster->GetEdgeOctree()->FindFirstElementWithBoundsTest((Cluster->BoundedEdges->GetData() + Edge.Index)->Bounds.GetBox(), ProcessOverlap);
}

#pragma endregion

#pragma region UPCGExEdgeRemoveOverlap

void UPCGExEdgeRemoveOverlap::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExEdgeRemoveOverlap* TypedOther = Cast<UPCGExEdgeRemoveOverlap>(Other))
	{
		Keep = TypedOther->Keep;
		Tolerance = TypedOther->Tolerance;
		bUseMinAngle = TypedOther->bUseMinAngle;
		MinAngle = TypedOther->MinAngle;
		bUseMaxAngle = TypedOther->bUseMaxAngle;
		MaxAngle = TypedOther->MaxAngle;
	}
}

#pragma endregion
