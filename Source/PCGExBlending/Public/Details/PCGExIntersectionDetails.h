// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExFuseDetails.h"
#include "PCGExIntersectionDetails.generated.h"

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExInclusionDetails
{
	GENERATED_BODY()

	/** Offset applied to projected polygon for inclusion tests. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	double InclusionOffset = 0;

	/** Percentage of points that can lie outside a path and still be considered inside it */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, ClampMin=0, ClampMax=1))
	double InclusionTolerance = 0;
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExUnionMetadataDetails
{
	GENERATED_BODY()

	/** Write a boolean marking points as union results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsUnion = false;

	/** Name of the attribute to mark point as union or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Is Union", EditCondition="bWriteIsUnion"))
	FName IsUnionAttributeName = "bIsUnion";

	/** Write the number of fused points to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteUnionSize = false;

	/** Name of the attribute to mark the number of fused point held */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Union Size", EditCondition="bWriteUnionSize"))
	FName UnionSizeAttributeName = "UnionSize";

	bool WriteAny() const
	{
		return bWriteIsUnion || bWriteUnionSize;
	}

	bool SanityCheck(FPCGExContext* InContext) const;
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExPointUnionMetadataDetails : public FPCGExUnionMetadataDetails
{
	GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExEdgeUnionMetadataDetails : public FPCGExUnionMetadataDetails
{
	GENERATED_BODY()

	/** Write a boolean marking edges created from subdivisions. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsSubEdge = false;

	/** Name of the attribute to mark edge as sub edge or not */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Is Sub Edge", EditCondition="bWriteIsSubEdge"))
	FName IsSubEdgeAttributeName = "SubEdge";
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExPointPointIntersectionDetails
{
	GENERATED_BODY()

	FPCGExPointPointIntersectionDetails()
	{
	}

	explicit FPCGExPointPointIntersectionDetails(const bool InSupportEdges);

	UPROPERTY()
	bool bSupportsEdges = true;

	/** Fuse Settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFuseDetails FuseDetails;

	/**  Point Union Data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExPointUnionMetadataDetails PointUnionData;

	/**  Edge Union Data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSupportsEdges", EditConditionHides, HideEditConditionToggle))
	FPCGExEdgeUnionMetadataDetails EdgeUnionData;

	bool WriteAny() const
	{
		return bSupportsEdges ? (PointUnionData.WriteAny() || EdgeUnionData.WriteAny()) : PointUnionData.WriteAny();
	}

	bool SanityCheck(FPCGExContext* InContext) const;
};

USTRUCT(BlueprintType)
struct PCGEXBLENDING_API FPCGExPointEdgeIntersectionDetails
{
	GENERATED_BODY()

	/** If disabled, points will only check edges they aren't mapped to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	bool bEnableSelfIntersection = true;

	/** Fuse Settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties, FullyExpand=true))
	FPCGExSourceFuseDetails FuseDetails;

	/** When enabled, point will be moved exactly on the edge. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSnapOnEdge = false;

	/** Write a boolean marking points that intersected an edge. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsIntersector = false;

	/** Name of the attribute to flag point as intersector (result of an Point/Edge intersection) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable, EditCondition="bWriteIsIntersector"))
	FName IsIntersectorAttributeName = "bIsIntersector";
};

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/intersections/edge-edge"))
struct PCGEXBLENDING_API FPCGExEdgeEdgeIntersectionDetails
{
	GENERATED_BODY()

	/** If disabled, edges will only be checked against other datasets. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	bool bEnableSelfIntersection = true;

	/** Distance at which two edges are considered intersecting. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=0))
	double Tolerance = DBL_INTERSECTION_TOLERANCE;
	double ToleranceSquared = DBL_INTERSECTION_TOLERANCE * DBL_INTERSECTION_TOLERANCE;

	/** Filter intersections by minimum angle between edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMinAngle = false;

	/** Minimum angle between edges to count as intersection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMinAngle", Units="Degrees", ClampMin=0, ClampMax=90))
	double MinAngle = 0;
	double MinDot = -1;

	/** Filter intersections by maximum angle between edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseMaxAngle = false;

	/** Maximum angle between edges to count as intersection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bUseMaxAngle", Units="Degrees", ClampMin=0, ClampMax=90))
	double MaxAngle = 90;
	double MaxDot = 1;

	//

	/** Write a boolean marking points created from edge crossings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCrossing = false;

	/** Name of the attribute to flag point as crossing (result of an Edge/Edge intersection) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable, EditCondition="bWriteCrossing"))
	FName CrossingAttributeName = "bCrossing";

	/** Will copy the flag values of attributes from the edges onto the point in order to filter them. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable))
	bool bFlagCrossing = false;

	/** Name of an int32 flag to fetch from the first edge */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable, EditCondition="bFlagCrossing"))
	FName FlagA;

	/** Name of an int32 flag to fetch from the second edge */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Metadata", meta=(PCG_Overridable, EditCondition="bFlagCrossing"))
	FName FlagB;

	void Init();

	FORCEINLINE bool CheckDot(const double InDot) const
	{
		return InDot <= MaxDot && InDot >= MinDot;
	}
};
