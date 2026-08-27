// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "Math/PCGExMath.h"
#include "PCGExEdgeRefineRemoveOverlap.generated.h"

UENUM()
enum class EPCGExEdgeOverlapPick : uint8
{
	Shortest = 0 UMETA(DisplayName = "Shortest", ToolTip="Keep the shortest edge"),
	Longest  = 1 UMETA(DisplayName = "Longest", ToolTip="Keep the longest edge"),
};

/**
 *
 */
class FPCGExEdgeRemoveOverlap : public FPCGExEdgeRefineOperation
{
public:
	virtual void PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics) override;
	virtual void ProcessEdge(PCGExGraphs::FEdge& Edge) override;

	EPCGExEdgeOverlapPick Keep = EPCGExEdgeOverlapPick::Longest;

	double Tolerance = DBL_INTERSECTION_TOLERANCE;
	double ToleranceSquared = DBL_INTERSECTION_TOLERANCE * DBL_INTERSECTION_TOLERANCE;

	bool bUseMinAngle = true;
	double MinAngle = 0;
	double MinDot = 1;

	bool bUseMaxAngle = true;
	double MaxAngle = 90;
	double MaxDot = -1;
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, meta=(DisplayName="Refine : Overlap", PCGExNodeLibraryDoc="clusters/refine/cluster-refine/refine-overlap"))
class UPCGExEdgeRemoveOverlap : public UPCGExEdgeRefineInstancedFactory
{
	GENERATED_BODY()

public:
	virtual bool WantsIndividualEdgeProcessing() const override
	{
		return true;
	}

	virtual bool WantsEdgeOctree() const override
	{
		return true;
	}

	virtual void CopySettingsFrom(const UPCGExInstancedFactory* Other) override;

	/** Which edge to keep when doing comparison. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExEdgeOverlapPick Keep = EPCGExEdgeOverlapPick::Longest;

	/** Distance at which two edges are considered intersecting. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	double Tolerance = DBL_INTERSECTION_TOLERANCE;

	/** Only consider overlaps whose crossing angle is at least Min Angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMinAngle = true;

	/** Min angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMinAngle", Units="Degrees", ClampMin=0, ClampMax=90))
	double MinAngle = 0;

	/** Only consider overlaps whose crossing angle is at most Max Angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMaxAngle = true;

	/** Maximum angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMaxAngle", Units="Degrees", ClampMin=0, ClampMax=90))
	double MaxAngle = 90;

	PCGEX_CREATE_REFINE_OPERATION(EdgeRemoveOverlap, { Operation->Keep = Keep; Operation->Tolerance = Tolerance; Operation->bUseMinAngle = bUseMinAngle; Operation->MinAngle = MinAngle; Operation->bUseMaxAngle = bUseMaxAngle; Operation->MaxAngle = MaxAngle; })
};
