// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Math/PCGExMathAxis.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExGraphDetails.generated.h"

namespace PCGExData
{
	struct FConstPoint;
	struct FMutablePoint;
}


UENUM()
enum class EPCGExBasicEdgeRadius : uint8
{
	Average = 0 UMETA(DisplayName = "Average", ToolTip="Edge radius is the average of each endpoint' bounds radii"),
	Lerp    = 1 UMETA(DisplayName = "Lerp", ToolTip="Edge radius is the edge lerp position between endpoint' bounds radii"),
	Min     = 2 UMETA(DisplayName = "Min", ToolTip="Edge radius is the smallest endpoint' bounds radius"),
	Max     = 3 UMETA(DisplayName = "Max", ToolTip="Edge radius is the largest endpoint' bounds radius"),
	Fixed   = 4 UMETA(DisplayName = "Fixed", ToolTip="Edge radius is a fixed size"),
};

USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExBasicEdgeSolidificationDetails
{
	GENERATED_BODY()

	FPCGExBasicEdgeSolidificationDetails() = default;

	/** Align the edge point to the edge direction over the selected axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExMinimalAxis SolidificationAxis = EPCGExMinimalAxis::None;

	/** Pick how edge radius should be calculated in regard to its endpoints */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_NotOverridable))
	EPCGExBasicEdgeRadius RadiusType = EPCGExBasicEdgeRadius::Lerp;

	/** Fixed edge radius */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_NotOverridable, EditCondition="RadiusType == EPCGExBasicEdgeRadius::Fixed", EditConditionHides))
	double RadiusConstant = 5;

	/** Scale the computed radius by a factor */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_NotOverridable, EditCondition="RadiusType != EPCGExBasicEdgeRadius::Fixed", EditConditionHides))
	double RadiusScale = 1;

	void Mutate(PCGExData::FMutablePoint& InEdgePoint, const PCGExData::FConstPoint& InStart, const PCGExData::FConstPoint& InEnd, const double InLerp) const;
};

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="clusters/common-settings/graph-builder-details"))
struct PCGEXGRAPHS_API FPCGExGraphBuilderDetails
{
	GENERATED_BODY()

	explicit FPCGExGraphBuilderDetails(const EPCGExMinimalAxis InDefaultSolidificationAxis = EPCGExMinimalAxis::None);

	/** Write edge point positions as an interpolation between their endpoints. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteEdgePosition = true;

	/** Edge position interpolation between start and end point positions. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, EditCondition="bWriteEdgePosition"))
	double EdgePosition = 0.5;

	/** If enabled, does some basic solidification of the edges over the X axis as a default. If you need full control, use the Edge Properties node. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_NotOverridable, DisplayName=" └─ Solidification", EditCondition="bWriteEdgePosition", HideEditConditionToggle))
	FPCGExBasicEdgeSolidificationDetails BasicEdgeSolidification;

	/** Don't output Clusters if they have less points than a specified amount. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bRemoveSmallClusters = false;

	/** Minimum points threshold (per cluster) */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, DisplayName=" ┌─ Min Vtx Count", EditCondition="bRemoveSmallClusters", ClampMin=2))
	int32 MinVtxCount = 3;

	/** Minimum edges threshold (per cluster) */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, DisplayName=" └─ Min Edge Count", EditCondition="bRemoveSmallClusters", ClampMin=1))
	int32 MinEdgeCount = 3;

	/** Don't output Clusters if they have more points than a specified amount. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bRemoveBigClusters = false;

	/** Maximum points threshold (per cluster) */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, DisplayName=" ┌─ Max Vtx Count", EditCondition="bRemoveBigClusters", ClampMin=2))
	int32 MaxVtxCount = 500;

	/** Maximum edges threshold (per cluster) */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable, DisplayName=" └─ Max Edge Count", EditCondition="bRemoveBigClusters", ClampMin=1))
	int32 MaxEdgeCount = 500;

	/** Refresh Edge Seed. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable))
	bool bRefreshEdgeSeed = false;

	/** If the use of cached clusters is enabled, output clusters along with the graph data. */
	UPROPERTY(BlueprintReadWrite, Category = Settings, EditAnywhere, meta = (PCG_Overridable))
	EPCGExOptionState BuildAndCacheClusters = EPCGExOptionState::Default;

	/** Pre-build planar face enumerator for cell-finding operations. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings|Pre-Built Cache", EditAnywhere, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bPreBuildFaceEnumerator = false;

	/** Projection settings for the pre-built face enumerator. Downstream nodes with matching projection will reuse the cache. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings|Pre-Built Cache", EditAnywhere, meta = (PCG_NotOverridable, DisplayName="Pre Build Face Enumerator", EditCondition="bPreBuildFaceEnumerator"))
	FPCGExGeo2DProjectionDetails FaceEnumeratorProjection;

	/** Pre-build node chains for path extraction and cluster simplification operations. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings|Pre-Built Cache", EditAnywhere, meta = (PCG_NotOverridable))
	bool bPreBuildChains = false;

	/** Write edge length to an attribute. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings|Extra Data", EditAnywhere, meta = (PCG_NotOverridable, InlineEditConditionToggle))
	bool bOutputEdgeLength = false;

	/** Name of the 'double' attribute edge length is written to. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings|Extra Data", EditAnywhere, meta = (PCG_Overridable, EditCondition="bOutputEdgeLength"))
	FName EdgeLengthName = FName("EdgeLength");

	bool WantsClusters() const;

	bool IsValid(const int32 NumVtx, const int32 NumEdges) const;
};
