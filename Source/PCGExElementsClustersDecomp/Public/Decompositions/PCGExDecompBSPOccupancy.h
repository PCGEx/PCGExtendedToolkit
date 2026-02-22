// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"
#include "Core/PCGExDecompOccupancyGrid.h"

#include "PCGExDecompBSPOccupancy.generated.h"

/**
 * BSP Occupancy decomposition operation.
 * Auto-detects voxel resolution from cluster edge lengths, then recursively splits
 * at axis-aligned planes through gaps/holes. Includes a contiguity post-pass
 * to ensure no cell contains disconnected occupied voxels.
 */
class FPCGExDecompBSPOccupancy : public FPCGExDecompositionOperation
{
public:
	EPCGExDecompTransformSpace TransformSpace = EPCGExDecompTransformSpace::Raw;
	FTransform CustomTransform = FTransform::Identity;
	EPCGExDecompVoxelSizeMode VoxelSizeMode = EPCGExDecompVoxelSizeMode::EdgeInferred;
	FVector VoxelSize = FVector(100.0);
	int32 MaxDepth = 20;
	int32 MinVoxelsPerCell = 4;
	double GapWeight = 2.0;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;

protected:
	struct FRegion
	{
		FIntVector Min;
		FIntVector Max; // Inclusive
	};

	/** Recursively split a region of the occupancy grid. */
	void SplitRecursive(
		const FPCGExDecompOccupancyGrid& Grid,
		const FRegion& Region,
		int32 Depth,
		TArray<int32>& VoxelCellIDs,
		int32& NextCellID) const;

	/** Count occupied voxels in a region. */
	int32 CountOccupied(
		const FPCGExDecompOccupancyGrid& Grid,
		const FRegion& Region) const;

	/** Find the best axis-aligned split plane for a region. Returns false if no good split exists. */
	bool FindBestSplit(
		const FPCGExDecompOccupancyGrid& Grid,
		const FRegion& Region,
		int32 TotalOccupied,
		int32& OutAxis,
		int32& OutPosition) const;

	/** Post-process: split non-contiguous occupied voxels within each leaf cell via flood fill. */
	void SplitNonContiguousCells(
		const FPCGExDecompOccupancyGrid& Grid,
		TArray<int32>& VoxelCellIDs,
		int32& NextCellID) const;
};

/**
 * Factory for BSP Occupancy decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : BSP Occupancy"))
class UPCGExDecompBSPOccupancy : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	/** How to orient the voxel grid. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDecompTransformSpace TransformSpace = EPCGExDecompTransformSpace::Raw;

	/** Custom transform for grid alignment. Only used when TransformSpace = Custom. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TransformSpace==EPCGExDecompTransformSpace::Custom", EditConditionHides))
	FTransform CustomTransform = FTransform::Identity;

	/** How to determine the voxel grid resolution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDecompVoxelSizeMode VoxelSizeMode = EPCGExDecompVoxelSizeMode::EdgeInferred;

	/** Manual voxel size. Only used when VoxelSizeMode = Manual. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="VoxelSizeMode==EPCGExDecompVoxelSizeMode::Manual", EditConditionHides))
	FVector VoxelSize = FVector(100.0);

	/** Maximum BSP recursion depth. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MaxDepth = 20;

	/** Stop splitting if a region has fewer occupied voxels than this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MinVoxelsPerCell = 4;

	/** Weight for the empty-gap bonus in split scoring. Higher values prefer splits through empty space. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="0"))
	double GapWeight = 2.0;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompBSPOccupancy, {
		Operation->TransformSpace = TransformSpace;
		Operation->CustomTransform = CustomTransform;
		Operation->VoxelSizeMode = VoxelSizeMode;
		Operation->VoxelSize = VoxelSize;
		Operation->MaxDepth = MaxDepth;
		Operation->MinVoxelsPerCell = MinVoxelsPerCell;
		Operation->GapWeight = GapWeight;
	})
};
