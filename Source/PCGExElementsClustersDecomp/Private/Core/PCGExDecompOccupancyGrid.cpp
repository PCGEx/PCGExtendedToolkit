// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExDecompOccupancyGrid.h"

#include "Math/PCGExBestFitPlane.h"

FVector FPCGExDecompOccupancyGrid::ResolveVoxelSize(
	const TSharedPtr<PCGExClusters::FCluster>& InCluster,
	const EPCGExDecompVoxelSizeMode Mode,
	const FVector& ManualVoxelSize)
{
	if (Mode == EPCGExDecompVoxelSizeMode::Manual)
	{
		return FVector(
			FMath::Max(ManualVoxelSize.X, KINDA_SMALL_NUMBER),
			FMath::Max(ManualVoxelSize.Y, KINDA_SMALL_NUMBER),
			FMath::Max(ManualVoxelSize.Z, KINDA_SMALL_NUMBER));
	}

	// EdgeInferred: compute average edge length
	if (!InCluster || InCluster->Nodes->Num() < 2) { return FVector(100.0); }

	const int32 NumNodes = InCluster->Nodes->Num();
	double TotalDist = 0;
	int32 EdgeCount = 0;

	for (int32 i = 0; i < NumNodes; i++)
	{
		const PCGExClusters::FNode* Node = InCluster->GetNode(i);
		if (!Node->bValid) { continue; }

		const FVector NodePos = InCluster->GetPos(i);
		for (const PCGExGraphs::FLink& Lk : Node->Links)
		{
			TotalDist += FVector::Dist(NodePos, InCluster->GetPos(Lk.Node));
			EdgeCount++;
		}
	}

	if (EdgeCount == 0) { return FVector(100.0); }

	const double AvgEdgeLength = FMath::Max(TotalDist / EdgeCount, KINDA_SMALL_NUMBER);
	return FVector(AvgEdgeLength);
}

bool FPCGExDecompOccupancyGrid::Build(
	const TSharedPtr<PCGExClusters::FCluster>& InCluster,
	const EPCGExDecompTransformSpace TransformSpace,
	const FVector& CellSize,
	const FTransform& CustomTransform)
{
	if (!InCluster || InCluster->Nodes->Num() == 0) { return false; }

	const int32 NumNodes = InCluster->Nodes->Num();

	// Ensure valid cell size
	const FVector SafeCellSize = FVector(
		FMath::Max(CellSize.X, KINDA_SMALL_NUMBER),
		FMath::Max(CellSize.Y, KINDA_SMALL_NUMBER),
		FMath::Max(CellSize.Z, KINDA_SMALL_NUMBER));

	// Compute WorldToGrid transform based on space mode
	switch (TransformSpace)
	{
	case EPCGExDecompTransformSpace::Raw:
		WorldToGrid = FTransform::Identity;
		GridToWorld = FTransform::Identity;
		break;

	case EPCGExDecompTransformSpace::BestFit:
		{
			const PCGExMath::FBestFitPlane BFP(NumNodes, [&](const int32 i) { return InCluster->GetPos(i); });
			GridToWorld = BFP.GetTransform();
			WorldToGrid = GridToWorld.Inverse();
		}
		break;

	case EPCGExDecompTransformSpace::Custom:
		GridToWorld = CustomTransform;
		WorldToGrid = CustomTransform.IsValid() ? CustomTransform.Inverse() : FTransform::Identity;
		break;
	}

	// Transform all node positions into grid-local space and compute local bounds
	TArray<FVector> LocalPositions;
	LocalPositions.SetNum(NumNodes);

	FBox LocalBounds(ForceInit);
	for (int32 i = 0; i < NumNodes; i++)
	{
		if (!InCluster->GetNode(i)->bValid) { continue; }
		LocalPositions[i] = WorldToGrid.TransformPosition(InCluster->GetPos(i));
		LocalBounds += LocalPositions[i];
	}

	if (!LocalBounds.IsValid) { return false; }

	LocalMin = LocalBounds.Min;

	// Compute grid dimensions.
	// Use FloorToInt+1 (not CeilToInt) so boundary nodes at exact multiples of CellSize
	// get their own voxel instead of being clamped into their neighbor's.
	const FVector BoundsSize = LocalBounds.Max - LocalBounds.Min;
	GridDimensions = FIntVector(
		FMath::Max(FMath::FloorToInt(BoundsSize.X / SafeCellSize.X) + 1, 1),
		FMath::Max(FMath::FloorToInt(BoundsSize.Y / SafeCellSize.Y) + 1, 1),
		FMath::Max(FMath::FloorToInt(BoundsSize.Z / SafeCellSize.Z) + 1, 1));

	TotalVoxels = GridDimensions.X * GridDimensions.Y * GridDimensions.Z;
	if (TotalVoxels <= 0) { return false; }

	// Initialize occupancy and mapping arrays
	Occupied.Init(false, TotalVoxels);
	VoxelToNodeIndex.SetNumUninitialized(TotalVoxels);
	for (int32& Idx : VoxelToNodeIndex) { Idx = -1; }

	NodeToVoxelIndex.SetNumUninitialized(NumNodes);
	for (int32& Idx : NodeToVoxelIndex) { Idx = -1; }

	// Quantize each valid node and populate occupancy
	int32 OccupiedCount = 0;
	for (int32 i = 0; i < NumNodes; i++)
	{
		if (!InCluster->GetNode(i)->bValid) { continue; }

		const FVector Rel = LocalPositions[i] - LocalMin;
		const FIntVector Coord = FIntVector(
			FMath::Clamp(FMath::FloorToInt(Rel.X / SafeCellSize.X), 0, GridDimensions.X - 1),
			FMath::Clamp(FMath::FloorToInt(Rel.Y / SafeCellSize.Y), 0, GridDimensions.Y - 1),
			FMath::Clamp(FMath::FloorToInt(Rel.Z / SafeCellSize.Z), 0, GridDimensions.Z - 1));

		const int32 Flat = FlatIndex(Coord.X, Coord.Y, Coord.Z);
		Occupied[Flat] = true;
		VoxelToNodeIndex[Flat] = i;
		NodeToVoxelIndex[i] = Flat;
		OccupiedCount++;
	}

	return OccupiedCount > 0;
}
