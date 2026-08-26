// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "Math/PCGExMathAxis.h"
#include "PCGExEdgeRefineSkeleton.generated.h"

/**
 *
 */
class FPCGExEdgeRefineSkeleton : public FPCGExEdgeRefineOperation
{
public:
	virtual void PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics) override;
	virtual void ProcessEdge(PCGExGraphs::FEdge& Edge) override;

	int8 ExchangeValue = 0;

	double Beta = 1;
	bool bInvert = false;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : β Skeleton", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-v-skeleton"))
class UPCGExEdgeRefineSkeleton : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool GetDefaultEdgeValidity() const override
	{
		return !bInvert;
	}

	virtual bool WantsNodeOctree() const override
	{
		return true;
	}

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	virtual bool WantsIndividualEdgeProcessing() const override
	{
		return true;
	}

	/** Beta parameter for the skeleton algorithm. Values ≤1 use lune-based tests, >1 use circle-based tests. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0.001))
	double Beta = 1;

	/** Invert the refinement result (keep edges that would be removed and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineSkeleton, { Operation->Beta = Beta; Operation->bInvert = bInvert; })
};
