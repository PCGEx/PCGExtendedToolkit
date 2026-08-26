// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "Details/PCGExCollisionDetails.h"
#include "Engine/HitResult.h"
#include "Math/PCGExMath.h"
#include "PCGExEdgeRefineLineTrace.generated.h"

struct FPCGExCollisionDetails;
/**
 *
 */
class FPCGExEdgeRefineLineTrace : public FPCGExEdgeRefineOperation
{
public:
	virtual void PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics) override;
	virtual void ProcessEdge(PCGExGraphs::FEdge& Edge) override;

	bool bTwoWayCheck = true;
	bool bInvert = false;
	int8 ExchangeValue = 0;

	bool bScatter = false;
	double ScatterSamples = 10;
	double ScatterRadius = 10;

	const FPCGExCollisionDetails* InitializedCollisionSettings = nullptr;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : Line Trace", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-line-trace"))
class UPCGExEdgeRefineLineTrace : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool GetDefaultEdgeValidity() const override
	{
		return !bInvert;
	}

	// Required for initializing collision settings
	virtual bool CanOnlyExecuteOnMainThread() const override
	{
		return true;
	}

	virtual void InitializeInContext(FPCGExContext* InContext, FName InOverridesPinLabel) override;
	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	virtual bool WantsIndividualEdgeProcessing() const override
	{
		return true;
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExCollisionDetails CollisionSettings;

	/** If the first linecast fails, tries the other way around. This is to ensure we don't fail against backfacing, but has high cost.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bTwoWayCheck = true;

	/** Scatter multiple traces around the endpoint to improve hit detection reliability. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bScatter = false;

	/** How many traces are fired per endpoint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" ├─ Samples", EditCondition="bScatter", EditConditionHides, ClampMin=1))
	double ScatterSamples = 10;

	/** World-space radius the extra traces are scattered within, around the endpoint. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Radius", EditCondition="bScatter", EditConditionHides))
	double ScatterRadius = 10;


	/** Invert the refinement result (keep edges that hit and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRefineLineTrace, { Operation->bTwoWayCheck = bTwoWayCheck; Operation->bInvert = bInvert; Operation->InitializedCollisionSettings = &InitializedCollisionSettings; Operation->bScatter = bScatter; Operation->ScatterSamples = FMath::Max(1, ScatterSamples); Operation->ScatterRadius = ScatterRadius; })

protected:
	FPCGExCollisionDetails InitializedCollisionSettings;
};
