// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Relaxations/PCGExBoxFittingRelax2.h"

#pragma region UPCGExBoxFittingRelax2

#if WITH_EDITOR
void UPCGExBoxFittingRelax2::ApplyShorthandDeprecation()
{
	ExtentsValue.Update(ExtentsInput_DEPRECATED, ExtentsAttribute_DEPRECATED, Extents_DEPRECATED);
}
#endif

void UPCGExBoxFittingRelax2::RegisterPrimaryBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterPrimaryBuffersDependencies(InContext, FacadePreloader);
	if (ExtentsValue.Input == EPCGExInputValueType::Attribute)
	{
		FacadePreloader.Register<FVector>(InContext, ExtentsValue.Attribute);
	}
}

bool UPCGExBoxFittingRelax2::PrepareForCluster(FPCGExContext* InContext, const TSharedPtr<PCGExClusters::FCluster>& InCluster)
{
	if (!Super::PrepareForCluster(InContext, InCluster))
	{
		return false;
	}

	ExtentsBuffer = ExtentsValue.GetValueSetting();
	if (!ExtentsBuffer->Init(PrimaryDataFacade))
	{
		return false;
	}

	return true;
}

void UPCGExBoxFittingRelax2::Step2(const PCGExClusters::FNode& Node)
{
	const FVector& CurrentPos = (ReadBuffer->GetData() + Node.Index)->GetLocation();
	const FVector& CurrentExtents = ExtentsBuffer->Read(Node.PointIndex) + FVector(Padding);

	// Build current node's bounds
	const FBox CurrentBox(CurrentPos - CurrentExtents, CurrentPos + CurrentExtents);

	// Apply repulsion forces between all pairs of nodes
	for (int32 OtherNodeIndex = Node.Index + 1; OtherNodeIndex < Cluster->Nodes->Num(); OtherNodeIndex++)
	{
		const PCGExClusters::FNode* OtherNode = Cluster->GetNode(OtherNodeIndex);
		const FVector& OtherPos = (ReadBuffer->GetData() + OtherNodeIndex)->GetLocation();
		const FVector& OtherExtents = ExtentsBuffer->Read(OtherNode->PointIndex) + FVector(Padding);

		// Build other node's bounds
		const FBox OtherBox(OtherPos - OtherExtents, OtherPos + OtherExtents);

		// Check for overlap
		if (!CurrentBox.Intersect(OtherBox))
		{
			continue;
		}

		// Calculate overlap in each axis
		const FVector OverlapMin = FVector::Max(CurrentBox.Min, OtherBox.Min);
		const FVector OverlapMax = FVector::Min(CurrentBox.Max, OtherBox.Max);
		const FVector OverlapSize = OverlapMax - OverlapMin;

		if (OverlapSize.X <= 0 || OverlapSize.Y <= 0 || OverlapSize.Z <= 0)
		{
			continue;
		}

		FVector SeparationDir;
		double SeparationMagnitude;

		switch (SeparationMode)
		{
		case EPCGExBoxFittingSeparation::MinimumPenetration:
		{
			// Find axis with minimum penetration
			const int32 MinAxis = (OverlapSize.X <= OverlapSize.Y && OverlapSize.X <= OverlapSize.Z) ? 0 : (OverlapSize.Y <= OverlapSize.Z) ? 1 : 2;

			SeparationDir = FVector::ZeroVector;
			SeparationDir[MinAxis] = (CurrentPos[MinAxis] < OtherPos[MinAxis]) ? -1.0 : 1.0;
			SeparationMagnitude = OverlapSize[MinAxis];
		}
		break;

		case EPCGExBoxFittingSeparation::EdgeDirection:
		{
			// Check if nodes are connected and use edge direction
			bool bConnected = false;
			for (const PCGExGraphs::FEdge& Edge : *Cluster->Edges)
			{
				if ((Cluster->GetEdgeStart(Edge)->Index == Node.Index && Cluster->GetEdgeEnd(Edge)->Index == OtherNodeIndex) ||
					(Cluster->GetEdgeStart(Edge)->Index == OtherNodeIndex && Cluster->GetEdgeEnd(Edge)->Index == Node.Index))
				{
					bConnected = true;
					break;
				}
			}

			if (bConnected)
			{
				SeparationDir = (OtherPos - CurrentPos).GetSafeNormal();
				SeparationMagnitude = FMath::Min3(OverlapSize.X, OverlapSize.Y, OverlapSize.Z);
			}
			else
			{
				// Fall back to minimum penetration for non-connected nodes
				const int32 MinAxis = (OverlapSize.X <= OverlapSize.Y && OverlapSize.X <= OverlapSize.Z) ? 0 : (OverlapSize.Y <= OverlapSize.Z) ? 1 : 2;
				SeparationDir = FVector::ZeroVector;
				SeparationDir[MinAxis] = (CurrentPos[MinAxis] < OtherPos[MinAxis]) ? -1.0 : 1.0;
				SeparationMagnitude = OverlapSize[MinAxis];
			}
		}
		break;

		case EPCGExBoxFittingSeparation::Centroid:
		default:
		{
			const FVector Delta = OtherPos - CurrentPos;
			const double Distance = Delta.Size();
			if (Distance <= KINDA_SMALL_NUMBER)
			{
				SeparationDir = FVector(1, 0, 0); // Arbitrary direction for coincident points
			}
			else
			{
				SeparationDir = Delta / Distance;
			}
			SeparationMagnitude = FMath::Min3(OverlapSize.X, OverlapSize.Y, OverlapSize.Z);
		}
			break;
		}

		// Apply separation force
		AddDelta(OtherNode->Index, Node.Index, RepulsionConstant * SeparationMagnitude * SeparationDir);
	}
}

#pragma endregion
