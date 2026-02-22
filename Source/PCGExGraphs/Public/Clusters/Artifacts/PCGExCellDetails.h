// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Clusters/PCGExEdge.h"

#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsDetails.h"
#include "Math/PCGExWinding.h"
#include "Math/PCGExMathAxis.h"
#include "Types/PCGExAttributeIdentity.h"
#include "PCGExCellDetails.generated.h"

namespace PCGExMT
{
	class FTaskManager;
}

class UPCGBasePointData;

namespace PCGExData
{
	struct FMutablePoint;
}

struct FPCGExNodeSelectionDetails;

namespace PCGExMath
{
	struct FTriangle;
}

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExCells
{
	namespace OutputLabels
	{
		const FName Paths = FName("Paths");
		const FName CellBounds = FName("CellBounds");
	}
}

///
/// Cell Triage - for bounded cell finding nodes
///

UENUM()
enum class EPCGExCellTriageOutput : uint8
{
	Separate = 0 UMETA(DisplayName = "Separate Pins", ToolTip="Output Inside/Touching/Outside to separate pins"),
	Combined = 1 UMETA(DisplayName = "Combined", ToolTip="Output matching cells to a single pin with triage tags"),
};

UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class EPCGExCellTriageFlags : uint8
{
	None     = 0,
	Inside   = 1 << 0 UMETA(DisplayName = "Inside", ToolTip="Output cells fully inside the bounds"),
	Touching = 1 << 1 UMETA(DisplayName = "Touching", ToolTip="Output cells touching/intersecting the bounds"),
	Outside  = 1 << 2 UMETA(DisplayName = "Outside", ToolTip="Output cells fully outside the bounds"),
};
ENUM_CLASS_FLAGS(EPCGExCellTriageFlags)

UENUM()
enum class EPCGExCellTriageResult : uint8
{
	Inside = 0,
	Touching,
	Outside
};

namespace PCGExCellTriage
{
	const FString TagInside = TEXT("CellTriage:Inside");
	const FString TagTouching = TEXT("CellTriage:Touching");
	const FString TagOutside = TEXT("CellTriage:Outside");

	// Default: Inside + Touching
	constexpr EPCGExCellTriageFlags DefaultFlags = static_cast<EPCGExCellTriageFlags>(
		static_cast<uint8>(EPCGExCellTriageFlags::Inside) | static_cast<uint8>(EPCGExCellTriageFlags::Touching));

	/** Classify a cell relative to bounds filter */
	PCGEXGRAPHS_API EPCGExCellTriageResult ClassifyCell(const FBox& CellBounds, const FVector& CellCentroid, const FBox& BoundsFilter);

	/** Get tag string for triage result */
	FORCEINLINE const FString& GetTriageTag(EPCGExCellTriageResult InResult)
	{
		switch (InResult)
		{
		case EPCGExCellTriageResult::Inside: return TagInside;
		case EPCGExCellTriageResult::Touching: return TagTouching;
		default: return TagOutside;
		}
	}

	/** Check if triage result is enabled by flags */
	FORCEINLINE bool IsEnabled(EPCGExCellTriageResult InResult, uint8 InFlags)
	{
		switch (InResult)
		{
		case EPCGExCellTriageResult::Inside: return !!(InFlags & static_cast<uint8>(EPCGExCellTriageFlags::Inside));
		case EPCGExCellTriageResult::Touching: return !!(InFlags & static_cast<uint8>(EPCGExCellTriageFlags::Touching));
		case EPCGExCellTriageResult::Outside: return !!(InFlags & static_cast<uint8>(EPCGExCellTriageFlags::Outside));
		default: return false;
		}
	}
}

UENUM()
enum class EPCGExPointPropertyOutput : uint8
{
	None      = 0 UMETA(DisplayName = "None", Tooltip="..."),
	Density   = 1 UMETA(DisplayName = "Density", Tooltip="..."),
	Steepness = 2 UMETA(DisplayName = "Steepness", Tooltip="..."),
	ColorR    = 3 UMETA(DisplayName = "R Channel", Tooltip="..."),
	ColorG    = 4 UMETA(DisplayName = "G Channel", Tooltip="..."),
	ColorB    = 5 UMETA(DisplayName = "B Channel", Tooltip="..."),
	ColorA    = 6 UMETA(DisplayName = "A Channel", Tooltip="..."),
};

UENUM()
enum class EPCGExCellOutputOrientation : uint8
{
	CCW = 0 UMETA(DisplayName = "Clockwise", Tooltip="..."),
	CW  = 1 UMETA(DisplayName = "Counter Clockwise", Tooltip="..."),
};

UENUM()
enum class EPCGExCellShapeTypeOutput : uint8
{
	Both        = 0 UMETA(DisplayName = "Convex & Concave", ToolTip="Output both convex and concave cells"),
	ConvexOnly  = 1 UMETA(DisplayName = "Convex Only", ToolTip="Output only convex cells"),
	ConcaveOnly = 2 UMETA(DisplayName = "Concave Only", ToolTip="Output only concave cells")
};

UENUM()
enum class EPCGExCellSeedLocation : uint8
{
	Original         = 0 UMETA(DisplayName = "Original", ToolTip="Seed position is unchanged"),
	Centroid         = 1 UMETA(DisplayName = "Centroid", ToolTip="Place the seed at the centroid of the path"),
	PathBoundsCenter = 2 UMETA(DisplayName = "Path bounds center", ToolTip="Place the seed at the center of the path' bounds"),
	FirstNode        = 3 UMETA(DisplayName = "First Node", ToolTip="Place the seed on the position of the node that started the cell."),
	LastNode         = 4 UMETA(DisplayName = "Last Node", ToolTip="Place the seed on the position of the node that ends the cell.")
};

UENUM()
enum class EPCGExCellSeedBounds : uint8
{
	Original           = 0 UMETA(DisplayName = "Original", ToolTip="Seed bounds is unchanged"),
	MatchCell          = 1 UMETA(DisplayName = "Match Cell", ToolTip="Seed bounds match cell bounds"),
	MatchPathResetQuat = 2 UMETA(DisplayName = "Match Cell (with rotation reset)", ToolTip="Seed bounds match cell bounds, and rotation is reset"),
};

UENUM()
enum class EPCGExCellSeedOwnership : uint8
{
	SeedOrder        = 0 UMETA(DisplayName = "Seed Order", ToolTip="First seed (by index order) wins ownership of a cell"),
	Closest          = 1 UMETA(DisplayName = "Closest", ToolTip="Closest seed to cell centroid (3D world distance) wins ownership"),
	ClosestProjected = 2 UMETA(DisplayName = "Closest (Projected)", ToolTip="Closest seed to cell centroid (2D projected distance) wins ownership"),
	BestCandidate    = 3 UMETA(DisplayName = "Best Candidate", ToolTip="Use sorting rules to determine which seed wins ownership"),
};

USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExCellConstraintsDetails
{
	GENERATED_BODY()

	FPCGExCellConstraintsDetails()
	{
	}

	explicit FPCGExCellConstraintsDetails(bool InUsedForPaths)
		: bUsedForPaths(InUsedForPaths)
	{
	}

	UPROPERTY()
	bool bUsedForPaths = false;

	/** Output winding order for cell paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bUsedForPaths", EditConditionHides, HideEditConditionToggle))
	EPCGExWinding OutputWinding = EPCGExWinding::CounterClockwise;

	/** Filter cells by convex/concave shape. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellShapeTypeOutput AspectFilter = EPCGExCellShapeTypeOutput::Both;

	/** Whether to keep cells that include dead ends wrapping */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bKeepCellsWithLeaves = true;

	/** Whether to duplicate dead end points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Duplicate Leaf points", EditCondition="bKeepCellsWithLeaves && bUsedForPaths", EditConditionHides, HideEditConditionToggle))
	bool bDuplicateLeafPoints = false;

	/** Omit cells that represent the outer boundary of the graph. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOmitWrappingBounds = true;

	UPROPERTY()
	double WrapperClassificationTolerance_DEPRECATED = 0.1;

	/** Keep the wrapper cell if it's the only cell found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Keep if Sole", EditCondition="bOmitWrappingBounds", EditConditionHides))
	bool bKeepWrapperIfSolePath = true;

	/** Omit cells below a minimum bounds size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowBoundsSize = false;

	/** Omit cells whose bounds size.length is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ┌─ Min Bounds Size", EditCondition="bOmitBelowBoundsSize", ClampMin=0))
	double MinBoundsSize = 3;

	/** Omit cells above a maximum bounds size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAboveBoundsSize = false;

	/** Omit cells whose bounds size.length is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Max Bounds Size", EditCondition="bOmitAboveBoundsSize", ClampMin=0))
	double MaxBoundsSize = 500;

	/** Omit cells below a minimum point count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowPointCount = false;

	/** Omit cells whose point count is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ┌─ Min Point Count", EditCondition="bOmitBelowPointCount", ClampMin=0))
	int32 MinPointCount = 3;

	/** Omit cells above a maximum point count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAbovePointCount = false;

	/** Omit cells whose point count is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Max Point Count", EditCondition="bOmitAbovePointCount", ClampMin=0))
	int32 MaxPointCount = 500;

	/** Omit cells below a minimum area. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowArea = false;

	/** Omit cells whose area is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ┌─ Min Area", EditCondition="bOmitBelowArea", ClampMin=0))
	double MinArea = 3;

	/** Omit cells above a maximum area. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAboveArea = false;

	/** Omit cells whose area is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Max Area", EditCondition="bOmitAboveArea", ClampMin=0))
	double MaxArea = 500;

	/** Omit cells below a minimum perimeter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowPerimeter = false;

	/** Omit cells whose perimeter is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ┌─ Min Perimeter", EditCondition="bOmitBelowPerimeter", ClampMin=0))
	double MinPerimeter = 3;

	/** Omit cells above a maximum perimeter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAbovePerimeter = false;

	/** Omit cells whose perimeter is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Max Perimeter", EditCondition="bOmitAbovePerimeter", ClampMin=0))
	double MaxPerimeter = 500;

	/** Omit cells with any segment below a minimum length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowSegmentLength = false;

	/** Omit cells that contains any segment which length is smaller than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ┌─ Min Segment Length", EditCondition="bOmitBelowSegmentLength", ClampMin=0))
	double MinSegmentLength = 3;

	/** Omit cells with any segment above a maximum length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAboveSegmentLength = false;

	/** Omit cells that contains any segment which length is larger than the specified amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Max Segment Length", EditCondition="bOmitAboveSegmentLength", ClampMin=0))
	double MaxSegmentLength = 500;

	/** Omit cells below a minimum compactness (0-1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitBelowCompactness = false;

	/** Omit cells whose compactness is smaller than the specified amount (0-1 range). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ┌─ Min Compactness", EditCondition="bOmitBelowCompactness", ClampMin=0, ClampMax=1))
	double MinCompactness = 0;

	/** Omit cells above a maximum compactness (0-1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAboveCompactness = false;

	/** Omit cells whose compactness is larger than the specified amount (0-1 range). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Max Compactness", EditCondition="bOmitAboveCompactness", ClampMin=0, ClampMax=1))
	double MaxCompactness = 1;
};

namespace PCGExClusters
{
	class FCell;
}

USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExCellSeedMutationDetails
{
	GENERATED_BODY()

	FPCGExCellSeedMutationDetails()
	{
	}

	explicit FPCGExCellSeedMutationDetails(bool InUsedForPaths)
		: bUsedForPaths(InUsedForPaths)
	{
	}

	UPROPERTY()
	bool bUsedForPaths = false;

	/** Filter cells by convex/concave shape for seed mutation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellShapeTypeOutput AspectFilter = EPCGExCellShapeTypeOutput::Both;

	/** Change the good seed position */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellSeedLocation Location = EPCGExCellSeedLocation::Centroid;

	/** Match seed bounds to cell bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMatchCellBounds = true;

	/** Reset seed scale to (1,1,1). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bResetScale = true;

	/** Reset seed rotation to identity. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bResetRotation = true;

	/** Output cell area to a point property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPointPropertyOutput AreaTo = EPCGExPointPropertyOutput::None;

	/** Output cell perimeter to a point property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPointPropertyOutput PerimeterTo = EPCGExPointPropertyOutput::None;

	/** Output cell compactness to a point property. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExPointPropertyOutput CompactnessTo = EPCGExPointPropertyOutput::None;

	void ApplyToPoint(const PCGExClusters::FCell* InCell, PCGExData::FMutablePoint& OutSeedPoint, const UPCGBasePointData* CellPoints) const;
};

USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExCellOBBAttributesDetails
{
	GENERATED_BODY()

	FPCGExCellOBBAttributesDetails()
	{
	}

	/** If enabled, use precise min box fit. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bUseMinBoxFit = false;
	
	/** Axis order for OBB transform orientation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExAxisOrder AxisOrder = EPCGExAxisOrder::XYZ;

	/** Min Extents */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector MinExtent = FVector(1);
};

USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExCellArtifactsDetails
{
	GENERATED_BODY()

	FPCGExCellArtifactsDetails()
	{
	}

	/** Output cells as closed paths (one path per cell) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputPaths = true;

	/** Output cells as OBB points (one point per cell) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputCellBounds = false;

	/** OBB-specific settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Settings", EditCondition="bOutputCellBounds", EditConditionHides))
	FPCGExCellOBBAttributesDetails OBBAttributes;

	/** Write cell unique hash to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCellHash = false;

	/** Write cell unique hash to an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Cell Hash", EditCondition="bWriteCellHash"))
	FName CellHashAttributeName = FName("CellHash");

	/** Write cell area to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteArea = false;

	/** Write cell area to an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Cell Area", EditCondition="bWriteArea"))
	FName AreaAttributeName = FName("Area");

	/** Write cell compactness (0-1) to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteCompactness = false;

	/** Write cell compactness to an attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Cell Compactness", EditCondition="bWriteCompactness"))
	FName CompactnessAttributeName = FName("Compactness");

	/** Write number of nodes in cell (OBB_Points mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNumNodes = false;

	/** Name of the num nodes attribute (Points mode only)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Num Nodes", EditCondition="bWriteNumNodes"))
	FName NumNodesAttributeName = FName("NumNodes");

	/** Vtx ID attribute (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteVtxId = false;

	/** Name of the attribute to write the vtx index of its point to. 
	 * This is useful if you want to find contours, mutate the cluster it comes from and remap the updated cluster positions onto the original cell. 
	 * (Paths mode only)
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Vtx ID", EditCondition="bWriteVtxId"))
	FName VtxIdAttributeName = FName("VtxId");

	/** Flag terminal points (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagTerminalPoint = false;

	/** Flag terminal points (Paths mode only)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Flag Terminal", EditCondition="bFlagTerminalPoint"))
	FName TerminalFlagAttributeName = FName("IsTerminal");

	/** Number of time a point is repeated in the cell (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNumRepeat = false;

	/** Number of time a point is repeated in the cell (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Repeat", EditCondition="bWriteNumRepeat"))
	FName NumRepeatAttributeName = FName("Repeat");


	/** Tag concave cells (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagConcave = false;

	/** Concave cell tag (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagConcave"))
	FString ConcaveTag = TEXT("Concave");

	/** Tag convex cells (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagConvex = false;

	/** Convex cell tag (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagConvex"))
	FString ConvexTag = TEXT("Convex");

	/** Tags to be forwarded from clusters (Paths mode only) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bOutputPaths", EditConditionHides))
	FPCGExNameFiltersDetails TagForwarding;

	bool WriteAny() const;
	bool Init(FPCGExContext* InContext);

	void Process(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExData::FFacade>& InDataFacade, const TSharedPtr<PCGExClusters::FCell>& InCell) const;
};

/**
 * Growth settings for cell expansion.
 * Allows seeds/holes to expand to adjacent cells.
 */
USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExCellGrowthDetails
{
	GENERATED_BODY()

	FPCGExCellGrowthDetails() = default;

	explicit FPCGExCellGrowthDetails(const int32 DefaultGrowth)
	{
		Growth = FPCGExInputShorthandSelectorInteger32Abs(FName("Growth"), DefaultGrowth);
	}

	/** Growth depth for expansion. 0 = no expansion, 1 = immediate neighbors, 2 = neighbors of neighbors, etc. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorInteger32Abs Growth = FPCGExInputShorthandSelectorInteger32Abs(FName("Growth"), 0);

	/** Initialize the growth reader from a facade (for per-point growth values). Captures min/max. */
	bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InFacade);

	/** Get growth value for a specific point index */
	FORCEINLINE int32 GetGrowth(const int32 PointIndex) const
	{
		return GrowthValue ? FMath::Max(0, GrowthValue->Read(PointIndex)) : 0;
	}

	/** Check if any growth is possible (max > 0) */
	FORCEINLINE bool HasPotentialGrowth() const
	{
		return GrowthValue && GrowthValue->Max() > 0;
	}

	/** Get the maximum possible growth value (for pre-allocation) */
	FORCEINLINE int32 GetMaxGrowth() const
	{
		return GrowthValue ? FMath::Max(0, GrowthValue->Max()) : 0;
	}

private:
	TSharedPtr<PCGExDetails::TSettingValue<int32>> GrowthValue;
};

namespace PCGExClusters
{
	/** Process cells as OBB points - outputs one PointData per cluster with one point per cell */
	PCGEXGRAPHS_API void ProcessCellsAsOBBPoints(
		const TSharedPtr<FCluster>& InCluster,
		const TArray<TSharedPtr<FCell>>& InCells,
		const TSharedPtr<PCGExData::FFacade>& OutFacade,
		const FPCGExCellArtifactsDetails& ArtifactSettings,
		const TSharedPtr<PCGExMT::FTaskManager>& TaskManager);
}
