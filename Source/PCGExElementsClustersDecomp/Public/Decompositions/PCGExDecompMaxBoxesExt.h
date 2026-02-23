// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExDataCommon.h"
#include "Core/PCGExDecompositionOperation.h"
#include "Core/PCGExDecompOccupancyGrid.h"

#include "PCGExDecompMaxBoxesExt.generated.h"

UENUM()
enum class EPCGExDecompWeightMode : uint8
{
	Multiplier = 0 UMETA(DisplayName = "Multiplier", ToolTip="High-weight boxes score higher during extraction. The algorithm naturally prefers bigger boxes around important nodes."),
	Priority   = 1 UMETA(DisplayName = "Priority", ToolTip="Two-pass extraction. Pass 1: only extract boxes whose average weight exceeds the threshold. Pass 2: standard extraction for the rest."),
};

/**
 * Max Boxes Extended decomposition operation.
 * Extends MaxBoxes with axis bias, per-node weight, volume preference, and heuristic merge gating.
 */
class FPCGExDecompMaxBoxesExt : public FPCGExDecompositionOperation
{
public:
	// --- Base MaxBoxes fields ---
	EPCGExDecompTransformSpace TransformSpace = EPCGExDecompTransformSpace::Raw;
	FTransform CustomTransform = FTransform::Identity;
	EPCGExDecompVoxelSizeMode VoxelSizeMode = EPCGExDecompVoxelSizeMode::EdgeInferred;
	FVector VoxelSize = FVector(100.0);
	FVector MaxCellSize = FVector(500.0);
	int32 MinVoxelsPerCell = 1;
	double Balance = 1.0;

	// --- Axis Bias ---
	FVector AxisBias = FVector(1.0);

	// --- Per-Node Weight ---
	FName WeightAttributeName = NAME_None;
	double DefaultWeight = 1.0;
	double WeightInfluence = 1.0;
	EPCGExDecompWeightMode WeightMode = EPCGExDecompWeightMode::Multiplier;
	double PriorityThreshold = 0.5;

	// --- Preferred Volume Range ---
	double PreferredMinVolume = 0;
	double PreferredMaxVolume = 0;
	double VolumePreferenceWeight = 1.0;

	// --- Heuristic Merge Gating ---
	bool bUseHeuristicMergeGating = false;
	double MergeScoreThreshold = 0.5;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;

protected:
	/**
	 * Find the largest axis-aligned box with extended scoring.
	 * Uses axis bias, weight prefix sums, and volume preference.
	 */
	bool FindLargestBox(
		const FPCGExDecompOccupancyGrid& Grid,
		const TBitArray<>& Available,
		const TArray<double>* WeightPrefixSums,
		FIntVector& OutMin,
		FIntVector& OutMax,
		int32& OutVolume) const;

	/** Post-process: iteratively merge adjacent cells that together form a perfect box.
	 *  Optionally gates merges using heuristic edge scores. */
	void MergeAdjacentCells(
		const FPCGExDecompOccupancyGrid& Grid,
		TArray<int32>& VoxelCellIDs,
		int32& NextCellID,
		const FIntVector& MaxExtent,
		const TArray<double>* EdgeScores) const;

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

	/** Build 3D prefix sum array for weights. Flat array indexed like the occupancy grid. */
	void BuildWeightPrefixSums(
		const FPCGExDecompOccupancyGrid& Grid,
		const TArray<double>& VoxelWeights,
		TArray<double>& OutPrefixSums) const;

	/** Query weight sum in box region via inclusion-exclusion on prefix sums. */
	double QueryWeightSum(
		const FPCGExDecompOccupancyGrid& Grid,
		const TArray<double>& PrefixSums,
		const FIntVector& BoxMin,
		const FIntVector& BoxMax) const;
};

/**
 * Factory for Max Boxes Extended decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Max Boxes (Extended)"))
class UPCGExDecompMaxBoxesExt : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	// --- Base MaxBoxes settings ---

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

	// --- Axis Bias ---

	/** Per-axis compactness penalty. Set low on axes where elongation is acceptable.
	 *  e.g., (0.1, 0.1, 1) for flat boxes, (1, 1, 0.1) for tall columns.
	 *  Works in grid-local space (post-transform). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Shape", meta=(PCG_Overridable))
	FVector AxisBias = FVector(1.0);

	// --- Per-Node Weight ---

	/** Source for per-node weight values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight", meta=(PCG_Overridable))
	EPCGExInputValueType WeightInput = EPCGExInputValueType::Constant;

	/** Attribute to read weight values from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight", meta=(PCG_Overridable, EditCondition="WeightInput==EPCGExInputValueType::Attribute", EditConditionHides))
	FPCGAttributePropertyInputSelector WeightAttribute;

	/** Default weight when using constant mode, or fallback for missing attribute values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight", meta=(PCG_Overridable))
	double DefaultWeight = 1.0;

	/** How strongly weights influence box extraction scoring.
	 *  0 = ignore weights, 1 = linear influence. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight", meta=(PCG_Overridable, ClampMin="0", EditCondition="WeightInput==EPCGExInputValueType::Attribute", EditConditionHides))
	double WeightInfluence = 1.0;

	/** How weights affect the extraction algorithm. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight", meta=(PCG_Overridable, EditCondition="WeightInput==EPCGExInputValueType::Attribute", EditConditionHides))
	EPCGExDecompWeightMode WeightMode = EPCGExDecompWeightMode::Multiplier;

	/** For Priority mode: minimum average weight for a box to be extracted in the first pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weight", meta=(PCG_Overridable, ClampMin="0", ClampMax="1", EditCondition="WeightInput==EPCGExInputValueType::Attribute&&WeightMode==EPCGExDecompWeightMode::Priority", EditConditionHides))
	double PriorityThreshold = 0.5;

	// --- Preferred Volume Range ---

	/** Soft preference for minimum box volume (in voxels). 0 = no minimum preference. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta=(PCG_Overridable, ClampMin="0"))
	double PreferredMinVolume = 0;

	/** Soft preference for maximum box volume (in voxels). 0 = no maximum preference. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta=(PCG_Overridable, ClampMin="0"))
	double PreferredMaxVolume = 0;

	/** How strongly the volume preference affects scoring. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta=(PCG_Overridable, ClampMin="0"))
	double VolumePreferenceWeight = 1.0;

	// --- Heuristic Merge Gating ---

	/** Enable heuristic-based merge control. When enabled, the Heuristics input pin is required. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Heuristics", meta=(PCG_Overridable))
	bool bUseHeuristicMergeGating = false;

	/** Boundary edge score above which merging is discouraged.
	 *  0 = only merge if all boundary edges score 0 (very strict).
	 *  1 = merge freely regardless of edge scores (effectively disabled). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Heuristics", meta=(PCG_Overridable, ClampMin="0", ClampMax="1", EditCondition="bUseHeuristicMergeGating", EditConditionHides))
	double MergeScoreThreshold = 0.5;

	virtual bool WantsHeuristics() const override { return bUseHeuristicMergeGating; }
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) override;
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompMaxBoxesExt, {
		Operation->TransformSpace = TransformSpace;
		Operation->CustomTransform = CustomTransform;
		Operation->VoxelSizeMode = VoxelSizeMode;
		Operation->VoxelSize = VoxelSize;
		Operation->MaxCellSize = MaxCellSize;
		Operation->MinVoxelsPerCell = MinVoxelsPerCell;
		Operation->Balance = Balance;
		Operation->AxisBias = AxisBias;
		Operation->WeightAttributeName = (WeightInput == EPCGExInputValueType::Attribute) ? WeightAttribute.GetName() : NAME_None;
		Operation->DefaultWeight = DefaultWeight;
		Operation->WeightInfluence = WeightInfluence;
		Operation->WeightMode = WeightMode;
		Operation->PriorityThreshold = PriorityThreshold;
		Operation->PreferredMinVolume = PreferredMinVolume;
		Operation->PreferredMaxVolume = PreferredMaxVolume;
		Operation->VolumePreferenceWeight = VolumePreferenceWeight;
		Operation->bUseHeuristicMergeGating = bUseHeuristicMergeGating;
		Operation->MergeScoreThreshold = MergeScoreThreshold;
	})
};
