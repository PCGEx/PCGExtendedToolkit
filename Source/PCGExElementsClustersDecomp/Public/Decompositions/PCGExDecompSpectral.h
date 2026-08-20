// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExDecompSpectral.generated.h"

UENUM()
enum class EPCGExDecompSpectralPartitionMode : uint8
{
	Natural  = 0 UMETA(DisplayName = "Natural", ToolTip="Cut at the Fiedler sign -- the graph's natural bottleneck. Preserves the most meaningful boundaries, but may produce fewer than NumPartitions when a cut is degenerate (unused budget is still recovered onto the sibling branch)."),
	Balanced = 1 UMETA(DisplayName = "Balanced", ToolTip="Cut at the Fiedler median so the two halves are even. Always yields a non-empty split, so it tracks NumPartitions closely; trades the natural bottleneck for balanced halves."),
	Exact    = 2 UMETA(DisplayName = "Exact", ToolTip="Reach exactly NumPartitions whenever the cluster has at least that many valid nodes: after balanced bisection, keep splitting the largest partition until the count is met. Extra cuts may be arbitrary when the graph has no further natural boundary."),
};

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
	EPCGExDecompSpectralPartitionMode PartitionMode = EPCGExDecompSpectralPartitionMode::Natural;

	virtual bool Decompose(FPCGExDecompositionResult& OutResult) override;

protected:
	/** Compute the Fiedler vector (2nd-smallest Laplacian eigenvector) for a subset via shifted
	 *  power iteration. Returns false only when the subset is too small or the spectrum is
	 *  degenerate; if the iteration reaches MaxIterations without fully converging, the best
	 *  current estimate is still returned. */
	bool ComputeFiedlerVector(
		const TArray<int32>& SubsetNodeIndices,
		TArray<double>& OutFiedler) const;

	/** Split a subset into two halves from its Fiedler vector, per the active PartitionMode
	 *  (Natural = sign cut; Balanced/Exact = balanced median cut). Returns false when no cut is
	 *  possible (fewer than 2 nodes, a degenerate spectrum, or -- in Natural mode -- an
	 *  all-one-sign vector). */
	bool BisectOnce(
		const TArray<int32>& NodeIndices,
		TArray<int32>& OutA,
		TArray<int32>& OutB) const;

	/** Recursive spectral bisection. Unused budget from a branch that can't split is handed to its
	 *  sibling so the realized partition count tracks the request as closely as the graph allows. */
	void BisectRecursive(
		const TArray<int32>& NodeIndices,
		int32 TargetPartitions,
		TArray<TArray<int32>>& OutPartitions) const;
};

/**
 * Factory for Spectral decomposition.
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Decompose : Spectral", PCGExNodeLibraryDoc="clusters/analyze/cluster-decomposition/decomp-spectral"))
class UPCGExDecompSpectral : public UPCGExDecompositionInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool WantsHeuristics() const override
	{
		return true;
	}

	/** How the requested partition count is reconciled with the graph's natural spectral cuts. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExDecompSpectralPartitionMode PartitionMode = EPCGExDecompSpectralPartitionMode::Natural;

	/** Target number of partitions to produce. Whether it is met exactly depends on Partition Mode
	 *  and graph connectivity (Exact reaches it when the cluster has at least that many valid nodes;
	 *  Natural may fall short on degenerate cuts). */
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
	                                     Operation->PartitionMode = PartitionMode;
	                                     })
};
