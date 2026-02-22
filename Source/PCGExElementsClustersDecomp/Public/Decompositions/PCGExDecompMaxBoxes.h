// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"
#include "Core/PCGExDecompOccupancyGrid.h"

#include "PCGExDecompMaxBoxes.generated.h"

/**
 * Max Boxes decomposition operation.
 * Auto-detects voxel resolution from cluster edge lengths, then iteratively extracts
 * the largest possible axis-aligned box. Boxes exceeding MaxCellSize are subdivided.
 * Every output cell is guaranteed to be a solid-filled rectangle.
 */
class FPCGExDecompMaxBoxes : public FPCGExDecompositionOperation
{
public:
	EPCGExDecompTransformSpace TransformSpace = EPCGExDecompTransformSpace::Raw;
	FTransform CustomTransform = FTransform::Identity;
	EPCGExDecompVoxelSizeMode VoxelSizeMode = EPCGExDecompVoxelSizeMode::EdgeInferred;
	FVector VoxelSize = FVector(100.0);
	FVector MaxCellSize = FVector(500.0);
	int32 MinVoxelsPerCell = 1;
	double Balance = 1.0;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;

protected:
	/**
	 * Find the largest axis-aligned box where ALL voxels are available.
	 * Uses the 2D histogram largest-rectangle method extended to 3D via Z-range iteration.
	 */
	bool FindLargestBox(
		const FPCGExDecompOccupancyGrid& Grid,
		const TBitArray<>& Available,
		FIntVector& OutMin,
		FIntVector& OutMax,
		int32& OutVolume) const;

	/** Subdivide a box into chunks that fit within MaxExtent, claim and assign CellIDs. */
	void SubdivideAndClaim(
		const FPCGExDecompOccupancyGrid& Grid,
		const FIntVector& BoxMin,
		const FIntVector& BoxMax,
		const FIntVector& MaxExtent,
		TBitArray<>& Available,
		TArray<int32>& VoxelCellIDs,
		int32& NextCellID,
		int32& RemainingCount,
		TArray<int32>& CellVoxelCounts) const;
};

/**
 * Factory for Max Boxes decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Max Boxes"))
class UPCGExDecompMaxBoxes : public UPCGExDecompositionInstancedFactory
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

	/** Maximum dimensions for output cells in world units. Extracted boxes larger than this are subdivided. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector MaxCellSize = FVector(500.0);

	/** Minimum occupied voxels per cell. Cells below this threshold are discarded (CellID = -1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MinVoxelsPerCell = 1;

	/** Penalizes elongated strips in favor of compact, cube-like boxes.
	 *  0 = pure volume (largest box first, may produce thin strips).
	 *  Higher values strongly prefer square-like shapes over thin rectangles. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="0"))
	double Balance = 1.0;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompMaxBoxes, {
		Operation->TransformSpace = TransformSpace;
		Operation->CustomTransform = CustomTransform;
		Operation->VoxelSizeMode = VoxelSizeMode;
		Operation->VoxelSize = VoxelSize;
		Operation->MaxCellSize = MaxCellSize;
		Operation->MinVoxelsPerCell = MinVoxelsPerCell;
		Operation->Balance = Balance;
	})
};
