// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExDecompConvexBSP.generated.h"

/**
 * Convex BSP decomposition operation.
 * Recursively splits the cluster using PCA-derived planes until each cell is "convex enough".
 */
class FPCGExDecompConvexBSP : public FPCGExDecompositionOperation
{
public:
	double MaxConcavityRatio = 0.01;
	int32 MinNodesPerCell = 4;
	int32 MaxCells = 32;
	int32 MaxDepth = 100;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;
};

/**
 * Factory for Convex BSP decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Convex BSP"))
class UPCGExDecompConvexBSP : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	/** Maximum allowed concavity ratio. 0 = all must be on hull. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double MaxConcavityRatio = 0.01;

	/** Minimum nodes per cell. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 MinNodesPerCell = 4;

	/** Maximum cells to produce. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 MaxCells = 32;

	/** Maximum recursion depth. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 MaxDepth = 100;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompConvexBSP, {
		Operation->MaxConcavityRatio = MaxConcavityRatio;
		Operation->MinNodesPerCell = MinNodesPerCell;
		Operation->MaxCells = MaxCells;
		Operation->MaxDepth = MaxDepth;
	})
};
