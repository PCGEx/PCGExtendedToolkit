// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsHandler.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Clusters/PCGExCluster.h"
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

	void Init(const int32 NumNodes)
	{
		NodeCellIDs.SetNumUninitialized(NumNodes);
		for (int32& ID : NodeCellIDs) { ID = -1; }
		NumCells = 0;
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

		if (bWantsNodeOctree) { Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Vtx); }
		if (bWantsEdgeOctree) { Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Edge); }

		if (bWantsHeuristics && Heuristics)
		{
			Heuristics->GetRoamingSeed();
			Heuristics->GetRoamingGoal();
		}
	}

	/** Main decomposition entry point. Must populate OutResult.NodeCellIDs and set OutResult.NumCells. */
	virtual bool Decompose(FPCGExDecompositionResult& OutResult)
	{
		return false;
	}

protected:
	TSharedPtr<PCGExClusters::FCluster> Cluster;
	TSharedPtr<PCGExHeuristics::FHandler> Heuristics;
};

/**
 * Abstract instanced factory for decomposition operations.
 */
UCLASS(Abstract, DefaultToInstanced, EditInlineNew, BlueprintType)
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

	virtual bool WantsNodeOctree() const { return false; }
	virtual bool WantsEdgeOctree() const { return false; }
	virtual bool WantsHeuristics() const { return false; }

	virtual TSharedPtr<FPCGExDecompositionOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);

protected:
	void PushSettings(const TSharedPtr<FPCGExDecompositionOperation>& Operation) const
	{
		Operation->bWantsNodeOctree = WantsNodeOctree();
		Operation->bWantsEdgeOctree = WantsEdgeOctree();
		Operation->bWantsHeuristics = WantsHeuristics();
	}
};
