// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "PCGExEdgeRefineGabriel.generated.h"

/**
 *
 */
class FPCGExEdgeRefineGabriel : public FPCGExEdgeRefineOperation
{
public:
	virtual void PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics) override;
	virtual void ProcessEdge(PCGExGraphs::FEdge& Edge) override;

	int8 ExchangeValue = 0;
	bool bInvert = false;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : Gabriel", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-gabriel"))
class UPCGExEdgeRefineGabriel : public UPCGExEdgeRefineInstancedFactory
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

	/** Invert the refinement result (keep edges that would be removed and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineGabriel, { Operation->bInvert = bInvert; })
};
