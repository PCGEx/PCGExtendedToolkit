// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSearchOperation.h"
#include "Core/PCGExSearchAllocations.h"
#include "Factories/PCGExFactoryData.h"

#include "UObject/Object.h"
#include "PCGExSearchBidirectional.generated.h"

namespace PCGEx
{
	class FScoredQueue;
	class FHashLookup;
	class FHashLookupArray;
}

class FPCGExHeuristicOperation;

namespace PCGExPathfinding
{
	/**
	 * Extended allocations for bidirectional search.
	 * Maintains separate data structures for forward and backward searches.
	 */
	class PCGEXELEMENTSPATHFINDING_API FBidirectionalSearchAllocations : public FSearchAllocations
	{
	public:
		// Backward search structures
		TBitArray<> VisitedBackward;
		TArray<double> GScoreBackward;
		TSharedPtr<PCGEx::FHashLookupArray> TravelStackBackward;
		TSharedPtr<PCGEx::FScoredQueue> ScoredQueueBackward;

		virtual void Init(const PCGExClusters::FCluster* InCluster) override;
		virtual void Reset() override;
	};
}

/**
 * Bidirectional Search operation.
 * Searches from both seed and goal simultaneously, meeting in the middle.
 */
class FPCGExSearchOperationBidirectional : public FPCGExSearchOperation
{
public:
	virtual bool ResolveQuery(
		const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
		const TSharedPtr<PCGExPathfinding::FSearchAllocations>& Allocations,
		const TSharedPtr<PCGExHeuristics::FHandler>& Heuristics,
		const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback = nullptr) const override;

	virtual TSharedPtr<PCGExPathfinding::FSearchAllocations> NewAllocations() const override;

protected:
	/** Reconstruct path from meeting point using both travel stacks */
	void ReconstructPath(
		const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
		int32 MeetingNode,
		PCGEx::FHashLookup* ForwardStack,
		PCGEx::FHashLookup* BackwardStack,
		int32 SeedIndex,
		int32 GoalIndex) const;
};

/**
 * Bidirectional Search Algorithm.
 * Searches from both the seed and goal simultaneously until they meet.
 * Can be significantly faster than unidirectional search for large graphs.
 * Time complexity is roughly O(b^(d/2)) instead of O(b^d) where b=branching factor, d=depth.
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Bidirectional", ToolTip ="Bidirectional Search. Searches from both ends simultaneously. Very fast for large graphs.", PCGExNodeLibraryDoc="pathfinding/algorithms/search-bidirectional"))
class UPCGExSearchBidirectional : public UPCGExSearchInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExSearchOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(SearchOperationBidirectional)
		NewOperation->bEarlyExit = bEarlyExit;
		return NewOperation;
	}
};
