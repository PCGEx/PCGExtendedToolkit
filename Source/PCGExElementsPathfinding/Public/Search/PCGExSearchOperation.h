// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCoreMacros.h"
#include "Factories/PCGExInstancedFactory.h"
#include "Factories/PCGExOperation.h"

#include "UObject/Object.h"

#include "PCGExSearchOperation.generated.h"

namespace PCGExHeuristics
{
	class FLocalFeedbackHandler;
	class FHandler;
}

namespace PCGExPathfinding
{
	class FSearchAllocations;
	class FPathQuery;
	struct FExtraWeights;
}

class FPCGExHeuristicOperation;

namespace PCGExClusters
{
	class FCluster;
}

class FPCGExSearchOperation : public FPCGExOperation
{
public:
	bool bEarlyExit = true;
	PCGExClusters::FCluster* Cluster = nullptr;

	virtual void PrepareForCluster(PCGExClusters::FCluster* InCluster);
	virtual bool ResolveQuery(
		const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
		const TSharedPtr<PCGExPathfinding::FSearchAllocations>& Allocations,
		const TSharedPtr<PCGExHeuristics::FHandler>& Heuristics,
		const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback = nullptr) const;

	virtual TSharedPtr<PCGExPathfinding::FSearchAllocations> NewAllocations() const;

	/** Grabs allocations from the pool, or creates new ones if the pool is empty. Thread-safe.
	 * ResolveQuery resets provided allocations on entry, so pooled ones come back dirty by design. */
	TSharedPtr<PCGExPathfinding::FSearchAllocations> AcquireAllocations();

	/** Returns allocations to the pool for reuse by other queries. Thread-safe. */
	void ReleaseAllocations(const TSharedPtr<PCGExPathfinding::FSearchAllocations>& InAllocations);

protected:
	/** Allocations a query runs on: the provided ones, reset; fresh ones when none were provided. */
	TSharedPtr<PCGExPathfinding::FSearchAllocations> PrepareAllocations(const TSharedPtr<PCGExPathfinding::FSearchAllocations>& InAllocations) const;

	/** Pool of reusable per-query search allocations */
	TArray<TSharedPtr<PCGExPathfinding::FSearchAllocations>> AllocationsPool;
	FCriticalSection AllocationsPoolLock;
};

/**
 * 
 */
UCLASS(Abstract)
class PCGEXELEMENTSPATHFINDING_API UPCGExSearchInstancedFactory : public UPCGExInstancedFactory
{
	GENERATED_BODY()

public:
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	virtual TSharedPtr<FPCGExSearchOperation> CreateOperation() const PCGEX_NOT_IMPLEMENTED_RET(CreateOperation(), nullptr);

	/** Exit the search early once a valid path is found. Disabling explores all possible paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bEarlyExit = true;
};
