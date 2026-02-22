// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "Clusters/PCGExCluster.h"

#include "PCGExDecompOccupancyGrid.generated.h"

UENUM()
enum class EPCGExDecompTransformSpace : uint8
{
	Raw     = 0 UMETA(DisplayName = "World", ToolTip="Use world axes. Assumes the cluster grid is already axis-aligned."),
	BestFit = 1 UMETA(DisplayName = "Best Fit", ToolTip="Auto-detect principal axes via PCA (FBestFitPlane)."),
	Custom  = 2 UMETA(DisplayName = "Custom", ToolTip="User-supplied transform defines the grid basis."),
};

UENUM()
enum class EPCGExDecompVoxelSizeMode : uint8
{
	EdgeInferred = 0 UMETA(DisplayName = "Edge Inferred", ToolTip="Auto-detect voxel size from average cluster edge length."),
	Manual       = 1 UMETA(DisplayName = "Manual", ToolTip="Use a user-specified voxel size."),
};

/**
 * Dense 3D boolean occupancy grid built from cluster node positions.
 * Shared helper for occupancy-based decomposition algorithms.
 */
struct FPCGExDecompOccupancyGrid
{
	FTransform WorldToGrid = FTransform::Identity;
	FTransform GridToWorld = FTransform::Identity;
	FVector LocalMin = FVector::ZeroVector;

	FIntVector GridDimensions = FIntVector::ZeroValue;
	int32 TotalVoxels = 0;

	TBitArray<> Occupied;

	/** Flat voxel index -> cluster NodeIndex (-1 if no node) */
	TArray<int32> VoxelToNodeIndex;

	/** Cluster NodeIndex -> flat voxel index (-1 if invalid/outside grid) */
	TArray<int32> NodeToVoxelIndex;

	/**
	 * Resolve the voxel size based on the mode.
	 * EdgeInferred computes average edge length from the cluster.
	 * Manual returns ManualVoxelSize as-is.
	 */
	static FVector ResolveVoxelSize(
		const TSharedPtr<PCGExClusters::FCluster>& InCluster,
		EPCGExDecompVoxelSizeMode Mode,
		const FVector& ManualVoxelSize);

	/**
	 * Build the occupancy grid from a cluster.
	 * @return false if the grid is degenerate (0 dimensions or 0 occupied voxels)
	 */
	bool Build(
		const TSharedPtr<PCGExClusters::FCluster>& InCluster,
		EPCGExDecompTransformSpace TransformSpace,
		const FVector& CellSize,
		const FTransform& CustomTransform = FTransform::Identity);

	FORCEINLINE int32 FlatIndex(const int32 X, const int32 Y, const int32 Z) const
	{
		return X + Y * GridDimensions.X + Z * GridDimensions.X * GridDimensions.Y;
	}

	FORCEINLINE FIntVector UnflatIndex(const int32 Flat) const
	{
		const int32 SliceSize = GridDimensions.X * GridDimensions.Y;
		const int32 Z = Flat / SliceSize;
		const int32 Remainder = Flat % SliceSize;
		const int32 Y = Remainder / GridDimensions.X;
		const int32 X = Remainder % GridDimensions.X;
		return FIntVector(X, Y, Z);
	}

	FORCEINLINE bool IsInBounds(const int32 X, const int32 Y, const int32 Z) const
	{
		return X >= 0 && X < GridDimensions.X &&
			Y >= 0 && Y < GridDimensions.Y &&
			Z >= 0 && Z < GridDimensions.Z;
	}

	FORCEINLINE bool IsOccupied(const int32 X, const int32 Y, const int32 Z) const
	{
		if (!IsInBounds(X, Y, Z)) { return false; }
		return Occupied[FlatIndex(X, Y, Z)];
	}
};
