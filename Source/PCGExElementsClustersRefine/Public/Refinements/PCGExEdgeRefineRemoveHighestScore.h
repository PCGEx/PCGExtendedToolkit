// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineRemoveHighestScore.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Remove Highest Score", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/score-based-refinements"))
class UPCGExEdgeRemoveHighestScore : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool WantsHeuristics() const override
	{
		return true;
	}

	virtual bool WantsIndividualNodeProcessing() const override
	{
		return true;
	}

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineBestLink, { Operation->bUseScore = true; Operation->bPickHighest = true; Operation->InitialBest = TNumericLimits<double>::Lowest(); Operation->Validity = 0; })
};
