// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExIntersectionDetails.h"
#include "Math/PCGExProjectionDetails.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "PCGExWritePathProperties.generated.h"

#define PCGEX_FOREACH_FIELD_PATH(MACRO)\
MACRO(PathLength, double, 0)\
MACRO(PathDirection, FVector, FVector::OneVector)\
MACRO(PathCentroid, FVector, FVector::ZeroVector)\
MACRO(IsClockwise, bool, true)\
MACRO(Area, double, 0)\
MACRO(Perimeter, double, 0)\
MACRO(Compactness, double, 0)\
MACRO(BoundingBoxCenter, FVector, FVector::ZeroVector)\
MACRO(BoundingBoxExtent, FVector, FVector::OneVector)\
MACRO(BoundingBoxOrientation, FQuat, FQuat::Identity)\
MACRO(InclusionDepth, int32, 0)\
MACRO(NumInside, int32, 0)


#define PCGEX_FOREACH_FIELD_PATH_POINT(MACRO)\
MACRO(Dot, double, 0)\
MACRO(Angle, double, 0)\
MACRO(DistanceToNext, double, 0)\
MACRO(DistanceToPrev, double, 0)\
MACRO(DistanceToStart, double, 0)\
MACRO(DistanceToEnd, double, 0)\
MACRO(PointTime, double, 0)\
MACRO(PointNormal, FVector, FVector::OneVector)\
MACRO(PointAvgNormal, FVector, FVector::OneVector)\
MACRO(PointBinormal, FVector, FVector::OneVector)\
MACRO(DirectionToNext, FVector, FVector::OneVector)\
MACRO(DirectionToPrev, FVector, FVector::OneVector)

namespace PCGExPaths
{
	class FPathInclusionHelper;
}

namespace PCGExPaths
{
	class FPolyPath;
}

namespace PCGExPaths
{
	class FPathEdgeAvgNormal;
	class FPathEdgeBinormal;
	class FPathEdgeLength;
	class FPath;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}


UENUM()
enum class EPCGExAttributeSetPackingMode : uint8
{
	PerInput = 0 UMETA(DisplayName = "Per Input", ToolTip="Emit one attribute set per input path, each holding a single entry and carrying that path's tags."),
	Merged   = 1 UMETA(DisplayName = "Merged", ToolTip="Emit a single attribute set holding one entry per input path, in input order. Tags are not carried."),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/analyze/path-properties"))
class UPCGExWritePathPropertiesSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(WritePathProperties, "Path : Properties", "One-stop node to compute useful path infos.");
#endif

protected:
	virtual bool OutputPinsCanBeDeactivated() const override
	{
		return bUseInclusionPins;
	}

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	/** Projection settings. Some path data must be computed on a 2D plane. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails();

	/** Inclusion details settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExInclusionDetails InclusionDetails;

	/** Attribute set packing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_Overridable, DisplayName="Packing"))
	EPCGExAttributeSetPackingMode PathAttributePackingMode = EPCGExAttributeSetPackingMode::Merged;

#pragma region Path attributes

	/** Whether to also write path attribute to the data set. Looks appealing, but can have massive memory cost -- this is legacy only.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_Overridable))
	bool bWritePathDataToPoints = true;

	/** Output Path Length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePathLength = false;

	/** Name of the 'double' attribute to write path length to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="PathLength", PCG_Overridable, EditCondition="bWritePathLength"))
	FName PathLengthAttributeName = FName("@Data.PathLength");

	/** Output averaged path direction. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePathDirection = false;

	/** Name of the 'FVector' attribute to write averaged direction to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="PathDirection", PCG_Overridable, EditCondition="bWritePathDirection"))
	FName PathDirectionAttributeName = FName("@Data.PathDirection");

	/** Output the average of every point position. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePathCentroid = false;

	/** Name of the 'FVector' attribute to write the path centroid to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="PathCentroid", PCG_Overridable, EditCondition="bWritePathCentroid"))
	FName PathCentroidAttributeName = FName("@Data.PathCentroid");

	/** Output path winding. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteIsClockwise = false;

	/** Name of the 'bool' attribute to write winding to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="Clockwise", PCG_Overridable, EditCondition="bWriteIsClockwise"))
	FName IsClockwiseAttributeName = FName("@Data.Clockwise");

	/** Output path area. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteArea = false;

	/** Name of the 'double' attribute to write area to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="Area", PCG_Overridable, EditCondition="bWriteArea"))
	FName AreaAttributeName = FName("@Data.Area");

	/** Output path perimeter. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePerimeter = false;

	/** Name of the 'double' attribute to write perimeter to (differ from length because this is the 2D projected value used to infer other values).*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="Perimeter", PCG_Overridable, EditCondition="bWritePerimeter"))
	FName PerimeterAttributeName = FName("@Data.Perimeter");

	/** Output path compactness. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteCompactness = false;

	/** Name of the 'double' attribute to write compactness to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="Compactness", PCG_Overridable, EditCondition="bWriteCompactness"))
	FName CompactnessAttributeName = FName("@Data.Compactness");


	/** Output the center of the minimum-volume oriented box around the path. Falls back to the data's axis-aligned bounds if the solve fails. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path|Oriented Bounding Box", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteBoundingBoxCenter = false;

	/** Name of the 'FVector' attribute to write bounding box center to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path|Oriented Bounding Box", meta=(DisplayName="Center", EditCondition="bWriteBoundingBoxCenter"))
	FName BoundingBoxCenterAttributeName = FName("@Data.OBBCenter");

	/** Output the half-size of the minimum-volume oriented box around the path. Falls back to the data's axis-aligned bounds if the solve fails. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path|Oriented Bounding Box", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteBoundingBoxExtent = false;

	/** Name of the 'FVector' attribute to write bounding box extent to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path|Oriented Bounding Box", meta=(DisplayName="Extent", EditCondition="bWriteBoundingBoxExtent"))
	FName BoundingBoxExtentAttributeName = FName("@Data.OBBExtent");

	/** Output the rotation of the minimum-volume oriented box around the path. Identity when the solve fails and the axis-aligned fallback is used. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path|Oriented Bounding Box", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteBoundingBoxOrientation = false;

	/** Name of the 'FQuat' attribute to write bounding box orientation to. **/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path|Oriented Bounding Box", meta=(DisplayName="Orientation", EditCondition="bWriteBoundingBoxOrientation"))
	FName BoundingBoxOrientationAttributeName = FName("@Data.OBBOrientation");

	/** Output path inclusion depth. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteInclusionDepth = false;

	/** Name of the 'int32' attribute to write inclusion depth to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="Inclusion Depth", PCG_Overridable, EditCondition="bWriteInclusionDepth"))
	FName InclusionDepthAttributeName = FName("@Data.InclusionDepth");

	/** Output path number of children. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteNumInside = false;

	/** Name of the 'int32' attribute to write how many paths are contained inside this one.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(DisplayName="Num Inside", PCG_Overridable, EditCondition="bWriteNumInside"))
	FName NumInsideAttributeName = FName("@Data.NumInside");

	/** Write a per-path 'Is Hole' flag attribute (true for odd inclusion-depth paths -- the standard even-odd hole rule). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Path", meta=(PCG_NotOverridable))
	bool bWriteIsHole = false;

#pragma endregion

#pragma region Points attributes

	/** Up Attribute constant */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta = (PCG_Overridable))
	FVector UpVector = FVector::UpVector;

	/** Output Dot product of Prev/Next directions. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDot = false;

	/** Name of the 'double' attribute to write the prev/next dot product to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="Dot", PCG_Overridable, EditCondition="bWriteDot"))
	FName DotAttributeName = FName("Dot");

	/** Output the angle between Prev/Next directions. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteAngle = false;

	/** Name of the 'double' attribute to write angle to next point to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="Angle", PCG_Overridable, EditCondition="bWriteAngle"))
	FName AngleAttributeName = FName("Angle");

	/** Unit/range to output the angle to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_Overridable, DisplayName=" └─ Range", EditCondition="bWriteAngle", EditConditionHides, HideEditConditionToggle))
	EPCGExAngleRange AngleRange = EPCGExAngleRange::PIRadians;

	/** Output distance to next. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDistanceToNext = false;

	/** Name of the 'double' attribute to write distance to next point to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="DistanceToNext", PCG_Overridable, EditCondition="bWriteDistanceToNext"))
	FName DistanceToNextAttributeName = FName("DistanceToNext");

	/** Output distance to prev. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDistanceToPrev = false;

	/** Name of the 'double' attribute to write distance to prev point to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="DistanceToPrev", PCG_Overridable, EditCondition="bWriteDistanceToPrev"))
	FName DistanceToPrevAttributeName = FName("DistanceToPrev");


	/** Output distance to start. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDistanceToStart = false;

	/** Name of the 'double' attribute to write distance to start to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="DistanceToStart", PCG_Overridable, EditCondition="bWriteDistanceToStart"))
	FName DistanceToStartAttributeName = FName("DistanceToStart");


	/** Output distance to end. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDistanceToEnd = false;

	/** Name of the 'double' attribute to write distance to end to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="DistanceToEnd", PCG_Overridable, EditCondition="bWriteDistanceToEnd"))
	FName DistanceToEndAttributeName = FName("DistanceToEnd");


	/** Output the point's normalized position along the path (0 at start, 1 at end). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePointTime = false;

	/** Name of the 'double' attribute to write point time to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="PointTime", PCG_Overridable, EditCondition="bWritePointTime"))
	FName PointTimeAttributeName = FName("PointTime");

	/** Output (1 - time) instead of time, so 0 is at end and 1 is at start. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, DisplayName=" └─ One Minus", EditCondition="bWritePointTime", HideEditConditionToggle))
	bool bTimeOneMinus = false;

	/** Output point normal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePointNormal = false;

	/** Name of the 'FVector' attribute to write point normal to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="PointNormal", PCG_Overridable, EditCondition="bWritePointNormal"))
	FName PointNormalAttributeName = FName("PointNormal");

	/** Output point averaged normal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePointAvgNormal = false;

	/** Name of the 'FVector' attribute to write point averaged normal to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="PointAverageNormal", PCG_Overridable, EditCondition="bWritePointAvgNormal"))
	FName PointAvgNormalAttributeName = FName("PointAvgNormal");

	/** Output point binormal. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWritePointBinormal = false;

	/** Name of the 'FVector' attribute to write point binormal to. The binormal is sign-matched to the segment normal, so it does not flip along the path.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="PointBinormal", PCG_Overridable, EditCondition="bWritePointBinormal"))
	FName PointBinormalAttributeName = FName("PointBinormal");

	/** Output the unit direction from each point to the next one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDirectionToNext = false;

	/** Name of the 'FVector' attribute to write direction to next point to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="DirectionToNext", PCG_Overridable, EditCondition="bWriteDirectionToNext"))
	FName DirectionToNextAttributeName = FName("DirectionToNext");

	/** Output the unit direction from each point to the previous one. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bWriteDirectionToPrev = false;

	/** Name of the 'FVector' attribute to write direction to prev point to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output - Points", meta=(DisplayName="DirectionToPrev", PCG_Overridable, EditCondition="bWriteDirectionToPrev"))
	FName DirectionToPrevAttributeName = FName("DirectionToPrev");

#pragma endregion

	/** Tag concave paths. Paths whose convexity cannot be resolved are left untagged. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bTagConcave = false;

	/** Tag added to concave paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagConcave"))
	FString ConcaveTag = TEXT("Concave");

	/** Tag convex paths. Paths whose convexity cannot be resolved are left untagged. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bTagConvex = false;

	/** Tag added to convex paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagConvex"))
	FString ConvexTag = TEXT("Convex");

	/** Tag paths that no other path encloses. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bTagOuter = false;

	/** Outer paths are not enclosed by any other path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagOuter"))
	FString OuterTag = TEXT("Outer");

	/** Tag paths enclosed by at least one other path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bTagInner = false;

	/** Inner paths are enclosed by one or more paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagInner"))
	FString InnerTag = TEXT("Inner");

	/** Tag paths sitting at an odd inclusion depth. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bTagOddInclusionDepth = false;

	/** Median paths are inner with a depth %2 != 0 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagOddInclusionDepth"))
	FString OddInclusionDepthTag = TEXT("OddDepth");

	/** Give each outer path and the holes directly inside it a shared value tag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_NotOverridable, InlineEditConditionToggle))
	bool bTagPairing = false;

	/** Pairs each outer path with the holes directly inside it: an outer (even inclusion depth) and the holes it immediately encloses share a unique integer id -- the outer's collection index -- written as a "Prefix:Id" value tag. Nested odd/even stacks (outer/hole/outer/hole) split into separate groups. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bTagPairing"))
	FString PairingTag = TEXT("PairingTag");

	/** If enabled, will output data to additional pins. Every output still goes to the default Path pin; the extra pins carry a filtered view of the same data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseInclusionPins = false;

	/** If enabled, outer path (inclusion depth of zero) will not be considered "odd" even if they technically are. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Outer is not Odd", EditCondition="bTagOddInclusionDepth || bUseInclusionPins", HideEditConditionToggle))
	bool bOuterIsNotOdd = true;

	bool CanForwardData() const;
	bool WantsInclusionHelper() const;
	bool WriteAnyPathData() const;
};

struct FPCGExWritePathPropertiesContext final : FPCGExPathProcessorContext
{
	friend class FPCGExWritePathPropertiesElement;

	PCGEX_FOREACH_FIELD_PATH_POINT(PCGEX_OUTPUT_DECL_TOGGLE)
	PCGEX_FOREACH_FIELD_PATH(PCGEX_OUTPUT_DECL_TOGGLE)

	TObjectPtr<UPCGParamData> PathAttributeSet;
	TArray<int64> MergedAttributeSetKeys;

	int32 NumOuter = 0;
	int32 NumInner = 0;
	int32 NumOdd = 0;

	TSharedPtr<PCGExPaths::FPathInclusionHelper> InclusionHelper;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExWritePathPropertiesElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(WritePathProperties)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExWritePathProperties
{
	const FName OutputPathProperties = TEXT("PathProperties");
	const FName OutputPathOuter = TEXT("Outer");
	const FName OutputPathInner = TEXT("Inner");
	const FName OutputPathMedian = TEXT("Odd");

	struct PCGEXELEMENTSPATHS_API FPointDetails
	{
		int32 Index;
		FVector Normal;
		FVector Binormal;
		FVector ToPrev;
		FVector ToNext;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExWritePathPropertiesContext, UPCGExWritePathPropertiesSettings>
	{
		friend class FBatch;

	protected:
		PCGEX_FOREACH_FIELD_PATH_POINT(PCGEX_OUTPUT_DECL)

		FPCGExGeo2DProjectionDetails ProjectionDetails;

		UPCGParamData* PathAttributeSet = nullptr;

		bool bClosedLoop = false;
		TSharedPtr<PCGExPaths::FPath> Path;
		TSharedPtr<PCGExPaths::FPathEdgeLength> PathLength;
		TSharedPtr<PCGExPaths::FPathEdgeBinormal> PathBinormal;
		TSharedPtr<PCGExPaths::FPathEdgeAvgNormal> PathAvgNormal;

		TArray<FPointDetails> Details;

		FVector UpConstant = FVector::ZeroVector;
		TSharedPtr<PCGExData::TBuffer<FVector>> UpGetter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
		virtual void Output() override;
	};

	class FBatch final : public PCGExPointsMT::TBatch<FProcessor>
	{
	public:
		explicit FBatch(FPCGExContext* InContext, const TArray<TWeakPtr<PCGExData::FPointIO>>& InPointsCollection);

	protected:
		virtual void OnInitialPostProcess() override;
	};
}
