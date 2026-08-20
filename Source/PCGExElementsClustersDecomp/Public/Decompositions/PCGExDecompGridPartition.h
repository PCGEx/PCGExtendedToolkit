// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExDecompGridPartition.generated.h"

UENUM()
enum class EPCGExDecompGridMergeMode : uint8
{
	Nearest  = 0 UMETA(DisplayName = "Nearest", ToolTip="Merge each under-populated cell into the globally nearest other cell by centroid. Established behaviour; cost grows with the square of the cell count, so it is best for moderate cell counts."),
	Adjacent = 1 UMETA(DisplayName = "Adjacent", ToolTip="Merge each under-populated cell into the largest grid-adjacent cell only. O(N) -- scales to very fine grids -- and keeps cells grid-contiguous; a spatially isolated under-populated cell is left as its own cell."),
};

/**
 * Grid partition decomposition operation.
 * Overlays a uniform 3D grid on the cluster bounding box and quantizes node positions to grid cells.
 */
class FPCGExDecompGridPartition : public FPCGExDecompositionOperation
{
public:
	FVector CellSize = FVector(100.0);
	int32 MinNodesPerCell = 1;
	EPCGExDecompGridMergeMode MergeMode = EPCGExDecompGridMergeMode::Nearest;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;
};

/**
 * Factory for Grid Partition decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Grid", PCGExNodeLibraryDoc="clusters/analyze/cluster-decomposition/decomp-grid-partition"))
class UPCGExDecompGridPartition : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	/** Size of each grid cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector CellSize = FVector(100.0);

	/** Minimum nodes per cell. Cells below this count are merged into a neighbour (see Merge Mode). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MinNodesPerCell = 1;

	/** How under-populated cells choose the neighbour they merge into. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDecompGridMergeMode MergeMode = EPCGExDecompGridMergeMode::Nearest;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompGridPartition, {
	                                     Operation->CellSize = CellSize;
	                                     Operation->MinNodesPerCell = MinNodesPerCell;
	                                     Operation->MergeMode = MergeMode;
	                                     })
};
