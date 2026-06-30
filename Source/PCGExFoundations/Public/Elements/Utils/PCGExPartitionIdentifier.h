// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "PCGExCoreSettingsCache.h"
#include "PCGPin.h"
#include "Core/PCGExSettings.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGCommon.h"

#include "PCGExPartitionIdentifier.generated.h"

UENUM()
enum class EPCGExPartitionSource : uint8
{
	ExecutingComponent = 0 UMETA(DisplayName = "Executing Component", ToolTip = "Introspect the partition the executing PCG component is generating in, plus any configured relatives. Outputs an attribute set."),
	InputPositions     = 1 UMETA(DisplayName = "Input Positions", ToolTip = "Classify each input point's position into a partition. Outputs the input points with the partition id(s) written per point."),
};

UENUM()
enum class EPCGExPartitionResolution : uint8
{
	FromComponent = 0 UMETA(DisplayName = "From Executing Component", ToolTip = "Use the executing component's grid size when it is partitioned; fall back to the explicit grid otherwise.", ActionIcon="Default"),
	Explicit      = 1 UMETA(DisplayName = "Explicit", ToolTip = "Always use the explicit grid set on this entry, ignoring the executing component.", ActionIcon="Constant"),
};

UENUM()
enum class EPCGExGrid2DMode : uint8
{
	Auto    = 0 UMETA(DisplayName = "Auto", ToolTip = "Follow the executing component (falls back to 3D when there is no component)."),
	Force2D = 1 UMETA(DisplayName = "2D", ToolTip = "Force a 2D grid: the Z coordinate is always 0 and dropped from the id."),
	Force3D = 2 UMETA(DisplayName = "3D", ToolTip = "Force a 3D grid."),
};

UENUM()
enum class EPCGExPartitionLayout : uint8
{
	Columns = 0 UMETA(DisplayName = "Columns", ToolTip = "Each grid entry becomes a suffixed attribute on the data (@Data) domain -- one set of values. The default."),
	Rows    = 1 UMETA(DisplayName = "Rows", ToolTip = "Each grid entry becomes its own row in the output attribute set, with the structured outputs per row."),
};

/** One partition query relative to the anchor (the executing cell, or each input point). Defaults describe the cell itself at the component's grid. */
USTRUCT(BlueprintType)
struct FPCGExPartitionGrid
{
	GENERATED_BODY()

	/** Appended to the partition id attribute name in Columns layout (e.g. "_North"); output as the per-row label in Rows layout. Leave as None for the primary 'self' entry. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FName Suffix = NAME_None;

	/** Integer cell offset from the resolved cell. (0,0,0) is the cell itself, (1,0,0) the next cell along X, etc. In 2D the Z offset is ignored. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FIntVector Offset = FIntVector::ZeroValue;

	/** How this entry's grid size is resolved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExPartitionResolution GridSizeResolution = EPCGExPartitionResolution::FromComponent;

	/** Grid size for this entry. Used when resolution is Explicit, and as the fallback when From Component has no partitioned component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (DisplayName = "Explicit Grid"))
	EPCGHiGenGrid ExplicitGrid = EPCGHiGenGrid::Grid256;

	/** Steps the resolved grid size along the power-of-two grid ladder: positive = coarser, negative = finer (e.g. -1 turns 800 into 400). Clamped to the exposed range (400 .. 204800). 0 leaves the resolved size untouched. Applies in both resolution modes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	int32 GridSizeOffset = 0;

	/** 2D/3D handling. Auto follows the executing component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExGrid2DMode Grid2D = EPCGExGrid2DMode::Auto;
};

/** Structured per-cell outputs. Executing Component only (suffixed on @Data in Columns, per-row in Rows). */
USTRUCT(BlueprintType)
struct FPCGExPartitionOutputs
{
	GENERATED_BODY()

	/** Write the integer grid coordinate as a vector attribute (X, Y, Z). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bGridCoord = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bGridCoord", EditConditionHides))
	FName GridCoordAttributeName = "GridCoord";

	/** Write the cell center (world space) as a vector attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bCellCenter = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bCellCenter", EditConditionHides))
	FName CellCenterAttributeName = "CellCenter";

	/** Write the cell min/max corners (world space) as vector attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bCellBounds = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bCellBounds", EditConditionHides))
	FName CellMinAttributeName = "CellMin";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bCellBounds", EditConditionHides))
	FName CellMaxAttributeName = "CellMax";

	/** Rows layout only: write each entry's Suffix as a label attribute (replaces the old Self/Neighbor/Parent relation). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bRowLabel = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bRowLabel", EditConditionHides))
	FName RowLabelAttributeName = "Partition";
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category = "Misc")
class UPCGExPartitionIdentifierSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPCGExPartitionIdentifierSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override
	{
		return FName(TEXT("PCGEx | Partition Identifier"));
	}

	virtual FText GetDefaultNodeTitle() const override
	{
		return FTEXT("PCGEx | Partition Identifier");
	}

	virtual FText GetNodeTooltipText() const override
	{
		return FTEXT("Computes deterministic PCG partition identifiers (size_x_y_z) for the executing component (and its neighbors/parents), or for arbitrary input positions. Use it to orchestrate loading and to format asset IDs against the partitions they belong to.");
	};

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Constant);
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	/** Where the queried position(s) come from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExPartitionSource Source = EPCGExPartitionSource::ExecutingComponent;

	/** Position to classify, read per input point. Defaults to the point location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Source == EPCGExPartitionSource::InputPositions", EditConditionHides))
	FPCGAttributePropertyInputSelector PositionSource;

	// -- Identifier --

	/** Write the formatted partition id string. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier")
	bool bOutputPartitionId = true;

	/** Name of the string attribute receiving the partition id (suffixed per grid entry in Columns layout). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (PCG_Overridable, EditCondition = "bOutputPartitionId", EditConditionHides))
	FName PartitionIdAttributeName = "PartitionId";

	/** Optional prefix prepended to the id. Empty by default -- add your own separator if you want one (e.g. "MyAsset_"). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (PCG_Overridable, EditCondition = "bOutputPartitionId && !bMatchEnginePartitionActorName", EditConditionHides))
	FString IdPrefix;

	/** Emit the exact engine partition actor name (PCGPartitionGridActor_... or its runtime variant) instead of the size_x_y_z form. Note: editor-only DataLayer/HLOD name suffixes are not reproduced. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Identifier", meta = (EditCondition = "bOutputPartitionId", EditConditionHides))
	bool bMatchEnginePartitionActorName = false;

	// -- Layout --

	/** Columns: one suffixed attribute per grid entry on the @Data domain. Rows: one attribute-set row per grid entry. Input Positions always writes per-point id columns. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (EditCondition = "Source == EPCGExPartitionSource::ExecutingComponent", EditConditionHides))
	EPCGExPartitionLayout Layout = EPCGExPartitionLayout::Columns;

	// -- Grids --

	/** The partitions to emit, relative to the anchor. The default single entry is the cell itself; add entries for neighbors (Offset) or parents (a coarser Explicit grid). An empty array emits nothing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (TitleProperty = "{Suffix}"))
	TArray<FPCGExPartitionGrid> Grids;

	// -- Outputs --

	/** Write the grid size (cm) as an int attribute, suffixed per grid entry. Available for both sources (written to the @Data domain). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs")
	bool bOutputGridSize = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable, EditCondition = "bOutputGridSize", EditConditionHides))
	FName GridSizeAttributeName = "GridSize";

	/** Structured per-cell outputs (Executing Component only): grid coordinate, cell center/bounds, and the Rows-layout label. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (EditCondition = "Source == EPCGExPartitionSource::ExecutingComponent", EditConditionHides))
	FPCGExPartitionOutputs Outputs;
};

class FPCGExPartitionIdentifierElement final : public IPCGElement
{
public:
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* Context) const override
	{
		return false;
	}

	virtual bool SupportsBasePointDataInputs(FPCGContext* InContext) const override
	{
		return true;
	}

protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};
