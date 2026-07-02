// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsHandler.h"
#include "Clusters/PCGExCluster.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"

#include "PCGExEdgeRefineOperation.generated.h"

#define PCGEX_CREATE_REFINE_OPERATION(_TYPE, _BODY) \
virtual TSharedPtr<FPCGExEdgeRefineOperation> CreateOperation() const override	{ \
TSharedPtr<FPCGEx##_TYPE> Operation = MakeShared<FPCGEx##_TYPE>(); _BODY \
PushSettings(Operation); return Operation;	}

/**
 * 
 */
class FPCGExEdgeRefineOperation : public FPCGExOperation
{
	friend class UPCGExEdgeRefineInstancedFactory;

protected:
	bool bWantsNodeOctree = false;
	bool bWantsEdgeOctree = false;
	bool bWantsHeuristics = false;

public:
	TArray<int8>* VtxFilterCache = nullptr;
	TArray<int8>* EdgeFilterCache = nullptr;

	// Set by the processor: whether edge filter factories are actually connected. Lets refinements with
	// optional filters stay inert when the filter pin is empty (EdgeFilterCache defaults to all-true).
	bool bHasEdgeFilters = false;

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

	virtual void Process()
	{
	}

	virtual void ProcessNode(PCGExClusters::FNode& Node)
	{
	}

	virtual void ProcessEdge(PCGExGraphs::FEdge& Edge)
	{
	}

protected:
	TSharedPtr<PCGExClusters::FCluster> Cluster;
	TSharedPtr<PCGExHeuristics::FHandler> Heuristics;
	mutable FRWLock EdgeLock;
	mutable FRWLock NodeLock;
};

/**
 * 
 */
UCLASS(Abstract)
class UPCGExEdgeRefineInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader)
	{
	}

	virtual void PrepareVtxFacade(const TSharedPtr<PCGExData::FFacade>& InVtxFacade) const
	{
	}

	virtual bool SupportFilters() const
	{
		return false;
	}

	virtual bool GetDefaultEdgeValidity() const
	{
		return true;
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

	virtual bool WantsIndividualNodeProcessing() const
	{
		return false;
	}

	virtual bool WantsIndividualEdgeProcessing() const
	{
		return false;
	}

	virtual TSharedPtr<FPCGExEdgeRefineOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);

protected:
	void PushSettings(const TSharedPtr<FPCGExEdgeRefineOperation>& Operation) const
	{
		Operation->bWantsNodeOctree = WantsNodeOctree();
		Operation->bWantsEdgeOctree = WantsEdgeOctree();
		Operation->bWantsHeuristics = WantsHeuristics();
	}
};
