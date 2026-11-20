// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineRemoveLongest.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Remove Longest", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/length-based-refinements"))
class UPCGExEdgeRemoveLongest : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool WantsIndividualNodeProcessing() const override
	{
		return true;
	}

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineBestLink, { Operation->bUseScore = false; Operation->bPickHighest = true; Operation->InitialBest = 0; Operation->Validity = 0; })
};
