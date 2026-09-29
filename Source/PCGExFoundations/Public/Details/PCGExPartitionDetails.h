// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"

#include "PCGExPartitionDetails.generated.h"

class IPCGGraphExecutionSource;
class UPCGComponent;

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

	/** 2D/3D handling. Auto follows the executing component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGExGrid2DMode Grid2D = EPCGExGrid2DMode::Auto;
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
		/** Only drives the id token count; Coord.Z is already 0 in 2D. */
		bool b2D = false;

		FCell() = default;

		FCell(const uint32 InGridSize, const FIntVector& InCoord, const bool bIn2D)
			: GridSize(InGridSize), Coord(InCoord), b2D(bIn2D)
		{
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
	};

	/**
	 * Steps a grid size (cm) along the power-of-two grid ladder (+ coarser / - finer), clamped to the editor-exposed
	 * range Grid4 (400) .. Grid2048 (204800). Offset 0 returns the input untouched, hidden and unbounded grids included.
	 */
	PCGEXFOUNDATIONS_API uint32 OffsetGridSize(const uint32 InGridSize, const int32 InOffset);

	/** Resolves a query's grid size (cm, never 0) and 2D flag against the executing component, which may be null. */
	PCGEXFOUNDATIONS_API void ResolveGrid(const FPCGExPartitionQuery& InQuery, const UPCGComponent* InComponent, uint32& OutGridSize, bool& bOut2D);

	/** The cell InAnchor falls in on an already resolved grid, moved by InOffset cells. */
	PCGEXFOUNDATIONS_API FCell MakeCell(const uint32 InGridSize, const bool bIn2D, const FIntVector& InOffset, const FVector& InAnchor);

	/** Resolves the query's grid, then the cell InAnchor falls in. */
	PCGEXFOUNDATIONS_API FCell MakeCell(const FPCGExPartitionQuery& InQuery, const FVector& InAnchor, const UPCGComponent* InComponent);

	/** '<Prefix>size_x_y' in 2D, '<Prefix>size_x_y_z' in 3D. The prefix is prepended as-is, separator included. */
	PCGEXFOUNDATIONS_API FString FormatId(const FCell& InCell, const FString& InPrefix = FString());

	/** Center of the source's bounds, the position its own partition resolves from. False without a source or valid bounds. */
	PCGEXFOUNDATIONS_API bool TryGetAnchor(const IPCGGraphExecutionSource* InSource, FVector& OutAnchor);
}
