// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsHandler.h"
#include "Clusters/PCGExCluster.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"

#include "PCGExDecompositionOperation.generated.h"

#define PCGEX_CREATE_DECOMPOSITION_OPERATION(_TYPE, _BODY) \
virtual TSharedPtr<FPCGExDecompositionOperation> CreateOperation() const override	{ \
TSharedPtr<FPCGEx##_TYPE> Operation = MakeShared<FPCGEx##_TYPE>(); _BODY \
PushSettings(Operation); return Operation;	}

/**
 * Lightweight result struct for decomposition operations.
 * NodeCellIDs maps NodeIndex -> CellID. Pre-sized by caller, init to -1.
 */
struct FPCGExDecompositionResult
{
	TArray<int32> NodeCellIDs;
	int32 NumCells = 0;

	/** Opt-in (set by the caller before Decompose): when true, operations populate CellSizes. */
	bool bWantsCellSizes = false;

	/** Per-cell bounds size (full extent), indexed by CellID, sized to NumCells. Empty unless requested. */
	TArray<FVector> CellSizes;

	void Init(const int32 NumNodes)
	{
		NodeCellIDs.SetNumUninitialized(NumNodes);
		for (int32& ID : NodeCellIDs)
		{
			ID = -1;
		}
		NumCells = 0;
		bWantsCellSizes = false;
		CellSizes.Reset();
	}
};

/**
 * Base class for decomposition operations.
 * Each operation receives a cluster and optionally heuristics,
 * and produces a mapping of NodeIndex -> CellID.
 */
class FPCGExDecompositionOperation : public FPCGExOperation
{
	friend class UPCGExDecompositionInstancedFactory;

protected:
	bool bWantsNodeOctree = false;
	bool bWantsEdgeOctree = false;
	bool bWantsHeuristics = false;

public:
	virtual void PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics = nullptr)
	{
		Cluster = InCluster;
		Heuristics = InHeuristics;

		if (bWantsNodeOctree)
		{
			Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Vtx);
		}
		if (bWantsEdgeOctree)
		{
			Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Edge);
		}

		if (bWantsHeuristics && Heuristics)
		{
			Heuristics->GetRoamingSeed();
			Heuristics->GetRoamingGoal();
		}
	}

	/**
	 * Framework entry point. Runs the decomposition, then guarantees the cell-size contract:
	 * when OutResult.bWantsCellSizes is set, CellSizes is always populated (sized to NumCells).
	 * Box-like operations fill it themselves inside Decompose(); any operation that doesn't
	 * falls back to the node-position AABB here -- so a decomposition can never silently emit
	 * empty/zero cell sizes, and new operations need not remember to handle the flag.
	 */
	bool DecomposeAndFinalize(FPCGExDecompositionResult& OutResult)
	{
		if (!Decompose(OutResult))
		{
			return false;
		}

		if (OutResult.bWantsCellSizes && OutResult.CellSizes.Num() != OutResult.NumCells)
		{
			ComputeNodeAABBCellSizes(OutResult);
		}

		check(!OutResult.bWantsCellSizes || OutResult.CellSizes.Num() == OutResult.NumCells);
		return true;
	}

protected:
	/** Main decomposition. Each algorithm overrides this and must populate OutResult.NodeCellIDs
	 *  and set OutResult.NumCells. Box-like operations should also fill OutResult.CellSizes (sized
	 *  to NumCells) when OutResult.bWantsCellSizes is set; operations that leave it untouched get a
	 *  node-position AABB default from DecomposeAndFinalize(). */
	virtual bool Decompose(FPCGExDecompositionResult& OutResult)
	{
		return false;
	}

	/**
	 * Shared helper for "cloud" decompositions that have no intrinsic box:
	 * fills OutResult.CellSizes with the full size of each cell's node-position AABB, in the
	 * WORLD-space frame (from Cluster->GetPos). This intentionally differs from the grid-local
	 * box sizes that voxel/grid decompositions report via FPCGExDecompOccupancyGrid::ComputeCellSizes.
	 * Call after NodeCellIDs and NumCells are final.
	 */
	void ComputeNodeAABBCellSizes(FPCGExDecompositionResult& OutResult) const
	{
		if (OutResult.NumCells <= 0)
		{
			return;
		}

		TArray<FBox> CellBounds;
		CellBounds.Init(FBox(ForceInit), OutResult.NumCells);

		const int32 NumNodes = OutResult.NodeCellIDs.Num();
		for (int32 i = 0; i < NumNodes; i++)
		{
			const int32 CellID = OutResult.NodeCellIDs[i];
			if (CellID < 0 || CellID >= OutResult.NumCells)
			{
				continue;
			}
			CellBounds[CellID] += Cluster->GetPos(i);
		}

		OutResult.CellSizes.SetNumUninitialized(OutResult.NumCells);
		for (int32 c = 0; c < OutResult.NumCells; c++)
		{
			OutResult.CellSizes[c] = CellBounds[c].IsValid ? CellBounds[c].GetSize() : FVector::ZeroVector;
		}
	}

	TSharedPtr<PCGExClusters::FCluster> Cluster;
	TSharedPtr<PCGExHeuristics::FHandler> Heuristics;
};

/**
 * Abstract instanced factory for decomposition operations.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType, meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-decomposition/decomposition-factory"))
class UPCGExDecompositionInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader)
	{
	}

	virtual void PrepareVtxFacade(const TSharedPtr<PCGExData::FFacade>& InVtxFacade) const
	{
	}

	virtual bool WantsNodeOctree() const
	{
		return false;
	}

	virtual bool WantsEdgeOctree() const
	{
		return false;
	}

	virtual bool WantsHeuristics() const
	{
		return false;
	}

	virtual TSharedPtr<FPCGExDecompositionOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);

protected:
	void PushSettings(const TSharedPtr<FPCGExDecompositionOperation>& Operation) const
	{
		Operation->bWantsNodeOctree = WantsNodeOctree();
		Operation->bWantsEdgeOctree = WantsEdgeOctree();
		Operation->bWantsHeuristics = WantsHeuristics();
	}
};
