// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/PCGExHashLookup.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "Utils/PCGExScoredQueue.h"
#include "PCGExEdgeRefinePrimMST.generated.h"

/**
 *
 */
class FPCGExEdgeRefinePrimMST : public FPCGExEdgeRefineOperation
{
public:
	virtual void Process() override;

	bool bInvert = false;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : MST (Prim)", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-mst-prim"))
class UPCGExEdgeRefinePrimMST : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool GetDefaultEdgeValidity() const override
	{
		return bInvert;
	}

	virtual bool WantsHeuristics() const override
	{
		return true;
	}

	/** Invert the refinement result (keep edges that would be removed and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefinePrimMST, { Operation->bInvert = bInvert; })
};
