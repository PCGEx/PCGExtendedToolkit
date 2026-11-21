// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSearchOperation.h"
#include "Core/PCGExSearchAllocations.h"
#include "Factories/PCGExFactoryData.h"

#include "UObject/Object.h"
#include "PCGExSearchBellmanFord.generated.h"

class FPCGExHeuristicOperation;

namespace PCGExPathfinding
{
	/**
	 * Allocations for Bellman-Ford. The search relaxes edges without going through the scored
	 * queue, so the sparse reset has nothing to track -- restore everything densely instead.
	 */
	class PCGEXELEMENTSPATHFINDING_API FBellmanFordSearchAllocations : public FSearchAllocations
	{
	public:
		virtual void Reset() override;
	};
}

/**
 * Bellman-Ford Search operation.
 * Can handle negative edge weights and detects negative cycles.
 */
class FPCGExSearchOperationBellmanFord : public FPCGExSearchOperation
{
public:
	/** If true, will detect negative cycles and fail if one is found */
	bool bDetectNegativeCycles = true;

	virtual bool ResolveQuery(
		const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
		const TSharedPtr<PCGExPathfinding::FSearchAllocations>& Allocations,
		const TSharedPtr<PCGExHeuristics::FHandler>& Heuristics,
		const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback = nullptr) const override;

	virtual TSharedPtr<PCGExPathfinding::FSearchAllocations> NewAllocations() const override;
};

/**
 * Bellman-Ford Search Algorithm.
 * Unlike Dijkstra and A*, can handle negative edge weights.
 * Also detects negative weight cycles.
 * Slower than A* (O(V*E) vs O(E log V)) but more robust.
 * Useful when heuristics may produce negative scores.
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Bellman-Ford", ToolTip ="Bellman-Ford Search. Handles negative edge weights and detects negative cycles. Slower but more robust.", PCGExNodeLibraryDoc="pathfinding/algorithms/search-bellman-ford"))
class UPCGExSearchBellmanFord : public UPCGExSearchInstancedFactory
{
	GENERATED_BODY()

public:
	/** If enabled, the search will fail if a negative weight cycle is detected */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bDetectNegativeCycles = true;

	virtual TSharedPtr<FPCGExSearchOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(SearchOperationBellmanFord)
		NewOperation->bEarlyExit = bEarlyExit;
		NewOperation->bDetectNegativeCycles = bDetectNegativeCycles;
		return NewOperation;
	}
};
