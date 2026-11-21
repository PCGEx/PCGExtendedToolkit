// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSearchOperation.h"
#include "Factories/PCGExFactoryData.h"


#include "UObject/Object.h"
#include "PCGExSearchAStar.generated.h"

class FPCGExSearchOperationAStar : public FPCGExSearchOperation
{
public:
	virtual bool ResolveQuery(
		const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
		const TSharedPtr<PCGExPathfinding::FSearchAllocations>& Allocations,
		const TSharedPtr<PCGExHeuristics::FHandler>& Heuristics,
		const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback = nullptr) const override;

	virtual TSharedPtr<PCGExPathfinding::FSearchAllocations> NewAllocations() const override;
};

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "A*", ToolTip ="A* Search. Returns early with the least possible amount of traversed nodes.", PCGExNodeLibraryDoc="pathfinding/algorithms/search-a"))
class UPCGExSearchAStar : public UPCGExSearchInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExSearchOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(SearchOperationAStar)
		NewOperation->bEarlyExit = bEarlyExit;
		return NewOperation;
	}
};
