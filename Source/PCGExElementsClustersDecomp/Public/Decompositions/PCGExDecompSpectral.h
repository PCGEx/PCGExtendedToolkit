// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExDecompSpectral.generated.h"

/**
 * Spectral decomposition operation.
 * Computes the graph Laplacian L=D-A, finds the Fiedler vector (2nd smallest eigenvector)
 * via shifted power iteration, and bisects by sign. Recursive for k-way partitioning.
 */
class FPCGExDecompSpectral : public FPCGExDecompositionOperation
{
public:
	int32 NumPartitions = 2;
	int32 MaxIterations = 200;
	double ConvergenceTolerance = 1e-6;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;

protected:
	/** Compute Fiedler vector for a subset of nodes. Returns false if convergence fails or graph is disconnected. */
	bool ComputeFiedlerVector(
		const TArray<int32>& SubsetNodeIndices,
		TArray<double>& OutFiedler) const;

	/** Recursive spectral bisection */
	void BisectRecursive(
		const TArray<int32>& NodeIndices,
		int32 TargetPartitions,
		TArray<TArray<int32>>& OutPartitions) const;
};

/**
 * Factory for Spectral decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Spectral"))
class UPCGExDecompSpectral : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool WantsHeuristics() const override { return true; }

	/** Number of partitions to produce. Must be a power of 2 for balanced bisection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="2"))
	int32 NumPartitions = 2;

	/** Maximum iterations for power iteration convergence. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="10"))
	int32 MaxIterations = 200;

	/** Convergence tolerance for eigenvector computation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	double ConvergenceTolerance = 1e-6;

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	PCGEX_CREATE_DECOMPOSITION_OPERATION(DecompSpectral, {
		Operation->NumPartitions = NumPartitions;
		Operation->MaxIterations = MaxIterations;
		Operation->ConvergenceTolerance = ConvergenceTolerance;
	})
};
