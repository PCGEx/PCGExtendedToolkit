// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineKeepShortest.generated.h"

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Keep Shortest", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/length-based-refinements"))
class UPCGExEdgeKeepShortest : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool GetDefaultEdgeValidity() const override
	{
		return false;
	}

	virtual bool WantsIndividualNodeProcessing() const override
	{
		return true;
	}

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineBestLink, { Operation->bUseScore = false; Operation->bPickHighest = false; Operation->InitialBest = TNumericLimits<double>::Max(); Operation->Validity = 1; })
};
