// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGPin.h"

#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExSettings.h"
#include "Details/PCGExPartitionDetails.h"

#include "PCGExPartitionGrid.generated.h"

namespace PCGExPartitionGrid
{
	inline const FName SourceBoundsLabel = TEXT("Bounds");
	inline const FName OutputPartitionsLabel = TEXT("Partitions");
	inline const FName OutputMetadataLabel = TEXT("Metadata");
}

UENUM()
enum class EPCGExPartitionCellBounds : uint8
{
	FullCell = 0 UMETA(DisplayName = "Full Cell", ToolTip = "The whole cell, as Get Actor Data reads it from the partition actor. In 2D, Z spans the reference bounds."),
	Clipped  = 1 UMETA(DisplayName = "Clipped to Reference", ToolTip = "The cell overlapped with the reference bounds: what the partition's own component reports as its bounds."),
};

/**
 * Partition Grid.
 * Outputs the partitions a partitioned PCG volume with the reference bounds would get on one grid, without creating
 * partition actors: one single-point data per cell, plus one attribute set per cell in the same order. Feed both to a
 * loop to process each cell as if it were its own partition.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Misc", meta = (Keywords = "pcgex partition cell grid higen proxy loop", PCGExNodeLibraryDoc = "utilities/partition-grid"))
class UPCGExPartitionGridSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExPartitionGridElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PartitionGrid, "Partition Grid", "Outputs one point per partition cell the reference bounds would be split into on a grid, with a matching attribute set per cell. Feed both to a loop to process each cell as if it were its own partition.");

	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(Constant); }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Grid the cells are laid on. From Component uses the executing component's grid when it runs as a partition. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (HideCellOffset))
	FPCGExPartitionQuery Grid;

	/** Bounds of each cell's point. The point itself always sits at the cell center, unrotated and unscaled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPartitionCellBounds PointBounds = EPCGExPartitionCellBounds::FullCell;

	/** World units added to each side of the point's bounds and, on X and Y, to the texture transform's half-size.
	 *  Negative values inset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector BoundsExpansion = FVector::ZeroVector;

	/** Also output the cells the reference bounds only touch on a face or an edge. The editor creates a partition actor
	 *  in those but never generates there, so they are skipped by default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIncludeTouchingCells = false;

	/** Fail rather than output more cells than this: a guard against a grid too fine for the reference bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = 1))
	int32 MaxCells = 65536;

	/** Write the partition id. InlineEditConditionToggle shows no tooltip of its own: Partition Id Attribute Name
	 *  carries it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOutputPartitionId = true;

	/** When enabled, write the partition id ('size_x_y' in 2D, 'size_x_y_z' in 3D) to this string attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (PCG_Overridable, EditCondition = "bOutputPartitionId"))
	FName PartitionIdAttributeName = "PartitionId";

	/** Prepended to the id as-is: add your own separator (e.g. "MyAsset_"). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (PCG_Overridable, EditCondition = "bOutputPartitionId && !bMatchEnginePartitionActorName", EditConditionHides))
	FString IdPrefix;

	/** Emit the name the editor gives partition actors (PCGPartitionGridActor_... or its runtime variant) instead.
	 *  DataLayer/HLOD name suffixes are not reproduced. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (EditCondition = "bOutputPartitionId", EditConditionHides, HideEditConditionToggle))
	bool bMatchEnginePartitionActorName = false;

	/** Write the grid size. InlineEditConditionToggle shows no tooltip of its own: Grid Size Attribute Name carries it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOutputGridSize = false;

	/** When enabled, write the grid size (cm) to this int attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, EditCondition = "bOutputGridSize"))
	FName GridSizeAttributeName = "GridSize";

	/** Grid coordinate, cell center and cell corners. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs")
	FPCGExPartitionCellOutputs Outputs;

	/** Write the texture transform. InlineEditConditionToggle shows no tooltip of its own: Texture Transform
	 *  Attribute Name carries it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOutputTextureTransform = true;

	/** When enabled, write to this attribute the transform that fits a texture to the cell, grown by Bounds Expansion on
	 *  X and Y. Feed it to Get Texture Data's Transform with Use Absolute Transform on. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, EditCondition = "bOutputTextureTransform"))
	FName TextureTransformAttributeName = "TextureTransform";
};

class FPCGExPartitionGridElement final : public IPCGExElement
{
public:
	/** Folds in what the executing component decides (resolved grid, 2D, runtime naming): the default CRC ignores it. */
	virtual void GetDependenciesCrc(const FPCGGetDependenciesCrcParams& InParams, FPCGCrc& OutCrc) const override;

protected:
	PCGEX_ELEMENT_CREATE_DEFAULT_CONTEXT

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
