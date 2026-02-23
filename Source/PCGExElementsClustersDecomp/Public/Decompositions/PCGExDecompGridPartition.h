// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExDecompGridPartition.generated.h"

/**
 * Grid partition decomposition operation.
 * Overlays a uniform 3D grid on the cluster bounding box and quantizes node positions to grid cells.
 */
class FPCGExDecompGridPartition : public FPCGExDecompositionOperation
{
public:
	FVector CellSize = FVector(100.0);
	int32 MinNodesPerCell = 1;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;
};

/**
 * Factory for Grid Partition decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Grid"))
class UPCGExDecompGridPartition : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	/** Size of each grid cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FVector CellSize = FVector(100.0);

	/** Minimum nodes per cell. Cells below this count are merged into the nearest neighbor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MinNodesPerCell = 1;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompGridPartition, {
		Operation->CellSize = CellSize;
		Operation->MinNodesPerCell = MinNodesPerCell;
	})
};
