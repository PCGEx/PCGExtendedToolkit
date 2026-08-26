// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineRemoveLowestScore.generated.h"

/**
 *
 */
class FPCGExEdgeRemoveLowestScore : public FPCGExEdgeRefineOperation
{
public:
	virtual void ProcessNode(PCGExClusters::FNode& Node) override;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Remove Lowest Score", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/score-based-refinements"))
class UPCGExEdgeRemoveLowestScore : public UPCGExEdgeRefineInstancedFactory
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

	PCGEX_CREATE_REFINE_OPERATION(EdgeRemoveLowestScore, {})
};
