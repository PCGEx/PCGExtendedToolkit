// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "Metadata/PCGMetadataCommon.h"

#include "PCGExPartitionDetails.generated.h"

class FPCGMetadataDomain;
class IPCGGraphExecutionSource;
class UPCGComponent;
struct FPCGContext;

template <typename T>
class FPCGMetadataAttribute;

UENUM()
enum class EPCGExPartitionResolution : uint8
{
	FromComponent = 0 UMETA(DisplayName = "From Executing Component", ToolTip = "Use the executing component's grid size when it runs as a partition (a local component); fall back to the explicit grid otherwise.", ActionIcon="Default"),
	Explicit      = 1 UMETA(DisplayName = "Explicit", ToolTip = "Always use the explicit grid set on this entry, ignoring the executing component.", ActionIcon="Constant"),
};

UENUM()
enum class EPCGExGrid2DMode : uint8
{
	Auto    = 0 UMETA(DisplayName = "Auto", ToolTip = "Follow the component the grid is resolved from (3D when there is none)."),
	Force2D = 1 UMETA(DisplayName = "2D", ToolTip = "Force a 2D grid: the Z coordinate is always 0 and dropped from the id."),
	Force3D = 2 UMETA(DisplayName = "3D", ToolTip = "Force a 3D grid."),
};

/** One partition query relative to the anchor (the executing cell, or each input point). Defaults describe the cell itself at the component's grid. */
USTRUCT(BlueprintType)
struct FPCGExPartitionQuery
{
	GENERATED_BODY()

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

	/** 2D/3D handling. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExGrid2DMode Grid2D = EPCGExGrid2DMode::Auto;
};

/** Per-cell geometry outputs: grid coordinate, cell center and cell corners. */
USTRUCT(BlueprintType)
struct FPCGExPartitionCellOutputs
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

	/** Write the cell min/max corners (world space) as vector attributes. In 2D, their Z spans [0, grid size]. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	bool bCellBounds = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bCellBounds", EditConditionHides))
	FName CellMinAttributeName = "CellMin";

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bCellBounds", EditConditionHides))
	FName CellMaxAttributeName = "CellMax";
};

namespace PCGExPartitionGrid
{
	/**
	 * A resolved partition cell. World-space derivations are anchored at the world origin, which is why they line up
	 * with real PCG partition actors; they mirror UPCGActorHelpers::GetCellCoord: the cell spans [Coord, Coord+1] * GridSize.
	 */
	struct FCell
	{
		/** In cm. */
		uint32 GridSize = 0;
		FIntVector Coord = FIntVector::ZeroValue;
		/** Drops Z from the id and unbounds the fixed bounds along Z; Coord.Z is already 0 in 2D. */
		bool b2D = false;

		FCell() = default;

		FCell(const uint32 InGridSize, const FIntVector& InCoord, const bool bIn2D)
			: GridSize(InGridSize), Coord(InCoord), b2D(bIn2D)
		{
		}

		bool operator==(const FCell& Other) const
		{
			return GridSize == Other.GridSize && b2D == Other.b2D && Coord == Other.Coord;
		}

		FVector CoordAsVector() const
		{
			return FVector(static_cast<double>(Coord.X), static_cast<double>(Coord.Y), static_cast<double>(Coord.Z));
		}

		FVector CellMinCorner() const
		{
			return CoordAsVector() * static_cast<double>(GridSize);
		}

		FVector CellCenter() const
		{
			return FVector(Coord.X + 0.5, Coord.Y + 0.5, Coord.Z + 0.5) * static_cast<double>(GridSize);
		}

		FVector CellMaxCorner() const
		{
			return FVector(Coord.X + 1.0, Coord.Y + 1.0, Coord.Z + 1.0) * static_cast<double>(GridSize);
		}

		/** The box UPCGSubsystem tests cells against; Z spans [0, GridSize] in 2D. */
		FBox CellBounds() const
		{
			return FBox(CellMinCorner(), CellMaxCorner());
		}
	};

	/**
	 * Steps a grid size (cm) along the power-of-two grid ladder (+ coarser / - finer), clamped to the editor-exposed
	 * range Grid4 (400) .. Grid2048 (204800). Offset 0 returns the input untouched, hidden and unbounded grids included.
	 */
	PCGEXFOUNDATIONS_API uint32 OffsetGridSize(const uint32 InGridSize, const int32 InOffset);

	/** Resolves a query's grid size (cm, never 0) against the executing component, which may be null. */
	PCGEXFOUNDATIONS_API uint32 ResolveGridSize(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent);

	/** False when the query resolves to an explicit grid with no size (Unbounded, Uninitialized), which ResolveGridSize maps to 1 cm. */
	PCGEXFOUNDATIONS_API bool HasBoundedGrid(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent);

	/** Resolves a query's 2D flag; Auto yields bInAuto2D. */
	PCGEXFOUNDATIONS_API bool ResolveGrid2D(const FPCGExPartitionQuery& InQuery, const bool bInAuto2D);

	/** Resolves a query's grid size (cm, never 0) and 2D flag against the executing component, which may be null. */
	PCGEXFOUNDATIONS_API void ResolveGrid(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent, uint32& OutGridSize, bool& bOut2D);

	/** What a node's queries read from the executing component: Add each query, then hand it to WarnIfComponentless. */
	struct FComponentReads
	{
		/** A From Executing Component grid size. */
		bool bGridSize = false;
		/** An Auto 2D mode that follows the executing component. */
		bool b2D = false;

		/** bInAuto2DFromComponent is false where Auto 2D follows another source than the executing component. */
		void Add(const FPCGExPartitionQuery& InQuery, const bool bInAuto2DFromComponent = true)
		{
			if (InQuery.GridSizeResolution == EPCGExPartitionResolution::FromComponent) { bGridSize = true; }
			if (bInAuto2DFromComponent && InQuery.Grid2D == EPCGExGrid2DMode::Auto) { b2D = true; }
		}
	};

	/**
	 * Graph warning when the reads need the executing component but no PCG component runs the node (componentless runtime
	 * generation, another execution source, or none): the grid size falls back to the Explicit Grid, Auto 2D to 3D.
	 */
	PCGEXFOUNDATIONS_API void WarnIfComponentless(const FComponentReads& InReads, const IPCGGraphExecutionSource* InSource, FPCGContext* InContext);

	/** The cell InAnchor falls in on an already resolved grid, moved by InOffset cells. */
	PCGEXFOUNDATIONS_API FCell MakeCell(const uint32 InGridSize, const bool bIn2D, const FIntVector& InOffset, const FVector& InAnchor);

	/** Resolves the query's grid, then the cell InAnchor falls in. */
	PCGEXFOUNDATIONS_API FCell MakeCell(const FPCGExPartitionQuery& InQuery, const FVector& InAnchor, const UPCGComponent* InComponent);

	/** '<Prefix>size_x_y' in 2D, '<Prefix>size_x_y_z' in 3D. The prefix is prepended as-is, separator included. */
	PCGEXFOUNDATIONS_API FString FormatId(const FCell& InCell, const FString& InPrefix = FString());

	/** Id prefix: the engine partition actor name prefix (runtime or not) when bInEngineName, InUserPrefix otherwise. */
	PCGEXFOUNDATIONS_API FString ResolveIdPrefix(const bool bInEngineName, const bool bInRuntime, const FString& InUserPrefix);

	/** Center of the source's bounds, the position its own partition resolves from. False without a source or valid bounds. */
	PCGEXFOUNDATIONS_API bool TryGetAnchor(const IPCGGraphExecutionSource* InSource, FVector& OutAnchor);

	/** The cell's partition actor bounds (APCGPartitionActor::GetFixedBounds): the cell, unbounded along Z in 2D. */
	PCGEXFOUNDATIONS_API FBox GetFixedBounds(const FCell& InCell);

	/** The transform Get Texture Data gives a texture in the cell's partition actor, in relative mode with an identity node transform. */
	PCGEXFOUNDATIONS_API FTransform GetTextureTransform(const FCell& InCell);

	/**
	 * The cells a partitioned component with these grid bounds gets, in the order UPCGSubsystem::ForAllOverlappingCells
	 * visits them (Z, then Y, then X). The count is exact and allocates nothing, so a caller can refuse it first.
	 */
	class PCGEXFOUNDATIONS_API FOverlappingCells
	{
	public:
		/** bInIncludeTouching keeps the cells the bounds only touch, which get a partition actor but never generate (IsCellGenerated). */
		FOverlappingCells(const FBox& InBounds, const uint32 InGridSize, const bool bIn2D, const bool bInIncludeTouching);

		/** Saturated at MAX_int64. -1 when the bounds are invalid, inverted or not finite, the grid size is 0, or their cell coordinates do not fit an int32. */
		int64 Num() const { return NumCells; }

		/** Replaces OutCells with the cells. False, with no cells, when Num() is negative or past MAX_int32. */
		bool GetCells(TArray<FCell>& OutCells) const;

	private:
		uint32 GridSize = 0;
		bool b2D = false;
		FIntVector MinCoord = FIntVector::ZeroValue;
		FIntVector MaxCoord = FIntVector::ZeroValue;
		int64 NumCells = -1;
		/** Whether each run combination's cells are kept, as [Z][Y][X]. Per axis, run 0 is the first coordinate, 1 the interior, 2 the last. */
		bool bKeep[3][3][3] = {};
	};

	/**
	 * Whether a partitioned component with these grid bounds generates in the cell (its partition actor gets a local
	 * component): a non-zero overlap volume, or any contact when the bounds have no volume. Mirrors the editor; runtime
	 * generation also skips every cell of bounds with no volume.
	 */
	PCGEXFOUNDATIONS_API bool IsCellGenerated(const FCell& InCell, const FBox& InBounds);

	/** False, with a graph warning, when InName is not a valid attribute name or is already in InOutUsed; the caller skips that output. */
	PCGEXFOUNDATIONS_API bool AcceptOutputName(const FName InName, TSet<FName>& InOutUsed, FPCGContext* InContext);

	/** Attribute names of the per-cell row columns; None skips a column. */
	struct FCellColumns
	{
		FName PartitionId = NAME_None;
		/** Prepended to the partition id as-is. */
		FString IdPrefix;
		FName GridSize = NAME_None;
		FName GridCoord = NAME_None;
		FName CellCenter = NAME_None;
		FName CellMin = NAME_None;
		FName CellMax = NAME_None;
	};

	/** Fills the enabled geometry columns of InOutputs, in declaration order, skipping names AcceptOutputName refuses. */
	PCGEXFOUNDATIONS_API void AddCellColumns(const FPCGExPartitionCellOutputs& InOutputs, FCellColumns& InOutColumns, TSet<FName>& InOutUsed, FPCGContext* InContext);

	/** Writes per-cell rows into one metadata domain. The constructor creates the columns, in FCellColumns order; InColumns must outlive the writer. */
	class PCGEXFOUNDATIONS_API FCellRowWriter
	{
	public:
		FCellRowWriter(FPCGMetadataDomain* InDomain, const FCellColumns& InColumns);

		void Write(const PCGMetadataEntryKey InKey, const FCell& InCell) const;

	private:
		const FString* IdPrefix = nullptr;
		FPCGMetadataAttribute<FString>* IdAttr = nullptr;
		FPCGMetadataAttribute<int32>* GridSizeAttr = nullptr;
		FPCGMetadataAttribute<FVector>* CoordAttr = nullptr;
		FPCGMetadataAttribute<FVector>* CenterAttr = nullptr;
		FPCGMetadataAttribute<FVector>* MinAttr = nullptr;
		FPCGMetadataAttribute<FVector>* MaxAttr = nullptr;
	};

	/**
	 * Writes on each entry the id of the cell its position falls in (MakeCell, then FormatId), in one batch: entries sharing
	 * a cell share one value, formatted once. InEntryKeys must already be entries of InDomain, one per position.
	 * False, writing nothing, when the attribute cannot be written.
	 */
	PCGEXFOUNDATIONS_API bool WriteCellIds(FPCGMetadataAttribute<FString>* InAttribute, FPCGMetadataDomain* InDomain, const TConstArrayView<PCGMetadataEntryKey> InEntryKeys, const TConstArrayView<FVector> InPositions, const uint32 InGridSize, const bool bIn2D, const FIntVector& InOffset, const FString& InPrefix);
}
