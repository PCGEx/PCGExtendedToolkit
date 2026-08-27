// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExFactories.h"

#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExPathHatch.generated.h"

class UPCGExSubPointsBlendInstancedFactory;
class FPCGExSubPointsBlendOperation;

namespace PCGExBlending
{
	class FUnionBlender;
}

namespace PCGExSampling
{
	class FSampingUnionData;
}

UENUM()
enum class EPCGExHatchBoxFitMode : uint8
{
	AxisAligned     = 0 UMETA(DisplayName = "Axis Aligned (AABB)", ToolTip="Use the axis-aligned bounding box of the projected input."),
	BestFit         = 1 UMETA(DisplayName = "Best Fit (OBB)", ToolTip="Use a PCA-derived oriented bounding box of the projected input."),
	BestFitAccurate = 2 UMETA(DisplayName = "Best Fit (OBB, Accurate)", ToolTip="Use a Min-box3-derived oriented bounding box of the projected input."),
};

UENUM()
enum class EPCGExHatchSpacingMode : uint8
{
	Count    = 0 UMETA(DisplayName = "Count", ToolTip="Fixed subdivision count."),
	Distance = 1 UMETA(DisplayName = "Distance", ToolTip="World-space spacing distance."),
};

UENUM()
enum class EPCGExHatchLineOrigin : uint8
{
	Start  = 0 UMETA(DisplayName = "Start", ToolTip="Lines start from the min perpendicular side of the box."),
	Center = 1 UMETA(DisplayName = "Center", ToolTip="Lines expand outward from the box center."),
};

UENUM()
enum class EPCGExHatchOutputMode : uint8
{
	Merged     = 0 UMETA(DisplayName = "Merged", ToolTip="One point data output per input, containing every generated point."),
	PerSegment = 1 UMETA(DisplayName = "Per Segment", ToolTip="One point data output per kept segment."),
};

/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/generate/path-hatch"))
class UPCGExPathHatchSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathHatch, "Path : Hatch", "Generate parallel line samples clipped by the input path silhouette.");
#endif

#if WITH_EDITORONLY_DATA
	virtual void PostInitProperties() override;
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	//~End UPCGExPointsProcessorSettings

	/** Projection used to flatten the input path before intersection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName="Projection", ShowOnlyInnerProperties))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Bounding box used to size and orient the line bundle in projected space. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExHatchBoxFitMode BoxFitMode = EPCGExHatchBoxFitMode::BestFit;

	/** Rotation (degrees) of the line direction around the projection plane normal. 0 = aligned with the box X axis; add 90 for the Y axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Lines", meta=(PCG_Overridable))
	FPCGExInputShorthandNameDouble AngleOffset = FPCGExInputShorthandNameDouble(FName("HatchAngle"), 0.0, false);

	/** How line spacing along the perpendicular axis is interpreted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Lines", meta=(PCG_NotOverridable))
	EPCGExHatchSpacingMode LineSpacingMode = EPCGExHatchSpacingMode::Count;

	/** Spacing value for lines. Interpreted as count or world distance depending on Line Spacing Mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Lines", meta=(PCG_Overridable))
	FPCGExInputShorthandNameDouble LineSpacing = FPCGExInputShorthandNameDouble(FName("HatchLineSpacing"), 100.0, false);

	/** Where the first line is anchored across the box. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Lines", meta=(PCG_NotOverridable))
	EPCGExHatchLineOrigin LineOrigin = EPCGExHatchLineOrigin::Center;

	/** Slide the line origin along the cross axis, as a normalized fraction of the available space.
	 *  Center: 1 = box edge along the cross axis (fraction of the half-extent), signed.
	 *  Start: 1 = opposite side along the cross axis (fraction of the full extent), signed. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Lines", meta=(PCG_Overridable))
	FPCGExInputShorthandNameDouble LineOriginSlide = FPCGExInputShorthandNameDouble(FName("HatchLineSlide"), 0.0, false);

	/** If enabled, lines pushed outside the cross-axis extent by the slide wrap around to the opposite side, keeping the box fully tiled. If disabled, shifted-out lines produce no crossings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Lines", meta=(PCG_NotOverridable))
	bool bWrapSlide = true;

	/** How sub-point spacing along each kept segment is interpreted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Segments", meta=(PCG_NotOverridable))
	EPCGExHatchSpacingMode SegmentSpacingMode = EPCGExHatchSpacingMode::Distance;

	/** Spacing value along each kept segment. Interpreted as count or world distance depending on Segment Spacing Mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Segments", meta=(PCG_Overridable))
	FPCGExInputShorthandNameDouble SegmentSpacing = FPCGExInputShorthandNameDouble(FName("@Data.Spacing"), 100.0, false);

	/** When using Distance mode, evenly redistribute computed sub-points across the segment. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Segments", meta=(PCG_Overridable, EditCondition="SegmentSpacingMode == EPCGExHatchSpacingMode::Distance", EditConditionHides))
	bool bRedistributeEvenly = false;

	/** Skip segments shorter than the configured minimum world-space length. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Segments", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFilterSmallSegments = false;

	/** Minimum world-space length to keep a segment. Shorter clipped segments are discarded. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Segments", meta=(PCG_Overridable, EditCondition="bFilterSmallSegments"))
	FPCGExInputShorthandNameDouble MinSegmentLength = FPCGExInputShorthandNameDouble(FName("@Data.MinSegmentLength"), 10, false);

	/** How the generated points are emitted: one data per input, or one data per kept segment. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_NotOverridable))
	EPCGExHatchOutputMode OutputMode = EPCGExHatchOutputMode::PerSegment;

	/** Write a per-point alpha value (0..1) along each kept segment. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAlpha = false;

	/** Attribute name receiving the segment alpha (0 at entry, 1 at exit). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, EditCondition="bWriteAlpha"))
	FName AlphaAttributeName = "Alpha";

	/** Blending applied to interior sub-points along each kept segment, using the segment's two endpoints as anchors. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Settings|Blending", Instanced, meta=(PCG_Overridable, ShowOnlyInnerProperties, NoResetToDefault))
	TObjectPtr<UPCGExSubPointsBlendInstancedFactory> Blending;

	/** If enabled, segment endpoint attributes are blended along the input polyline edges they intersect (edge-T weighted lerp). Otherwise each endpoint inherits the closest input point's metadata as-is. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable))
	bool bDoEndpointBlending = false;

	/** Blending mode used for endpoint attributes when Endpoint Blending is enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable, EditCondition="bDoEndpointBlending"))
	FPCGExBlendingDetails EndpointBlending = FPCGExBlendingDetails(EPCGExBlendingType::Weight, EPCGExBlendingType::None);

	/** Carry-over rules for endpoint blending. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable, EditCondition="bDoEndpointBlending"))
	FPCGExCarryOverDetails EndpointCarryOver;
};

struct FPCGExPathHatchContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathHatchElement;

	UPCGExSubPointsBlendInstancedFactory* Blending = nullptr;
	FPCGExBlendingDetails EndpointBlending;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathHatchElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathHatch)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathHatch
{
	struct FCrossing
	{
		int32 EdgeI0 = -1;
		int32 EdgeI1 = -1;
		double TAlongLine = 0.0;
		float EdgeT = 0.0f;
	};

	struct FHatchSegment
	{
		int32 StartEdgeI0 = -1;
		int32 StartEdgeI1 = -1;
		float StartEdgeT = 0.0f;

		int32 EndEdgeI0 = -1;
		int32 EndEdgeI1 = -1;
		float EndEdgeT = 0.0f;

		FVector WorldStart = FVector::ZeroVector;
		FVector WorldEnd = FVector::ZeroVector;
		double Length = 0.0;

		int32 SourceStart = -1;
		int32 SourceEnd = -1;

		// OutStart only used in Merged mode (offset into the shared output buffer); 0 per-segment.
		int32 OutStart = -1;
		int32 NumPoints = 0;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathHatchContext, UPCGExPathHatchSettings>
	{
		// Hot-path settings caches (read in ProcessRange's parallel loop).
		bool bUseSegmentCount = false;
		bool bPerSegmentOutput = false;
		bool bWriteAlpha = false;
		bool bRedistributeEvenly = false;

		FPCGExGeo2DProjectionDetails Projection;
		TArray<FVector2D> Projected;

		FVector2D BoxAxisX = FVector2D(1, 0);
		FVector2D BoxAxisY = FVector2D(0, 1);
		FVector2D BoxCenter = FVector2D::ZeroVector;
		FVector2D BoxHalfExtents = FVector2D::ZeroVector;

		FVector2D LineDir2D = FVector2D(1, 0);
		FVector2D LinePerp2D = FVector2D(0, 1);

		double AngleOffsetDeg = 0.0;
		double LineSpacingValue = 0.0;
		double LineOriginSlideValue = 0.0;
		double SegmentSpacingValue = 0.0;
		double MinSegmentLengthValue = 0.0;

		TArray<FHatchSegment> Segments;

		TSharedPtr<PCGExData::FPointIO> MergedIO;
		TSharedPtr<PCGExData::FFacade> MergedOutputFacade;
		TArray<TSharedPtr<PCGExData::FPointIO>> SegmentIOs;
		TArray<TSharedPtr<PCGExData::FFacade>> SegmentFacades;

		// Pre-created in CompleteWork so the parallel pass's SetValue calls don't race on buffer creation.
		TSharedPtr<PCGExData::TBuffer<double>> AlphaWriter;
		TArray<TSharedPtr<PCGExData::TBuffer<double>>> SegmentAlphaWriters;

		TSet<FName> ProtectedAttributes;

		// Endpoint blender (optional): per-output-point lerp from input edge vertices via a resettable
		// weighted union. One per output facade.
		TSharedPtr<PCGExBlending::FUnionBlender> EndpointBlender;
		TArray<TSharedPtr<PCGExBlending::FUnionBlender>> EndpointBlenders;

		// Interior blender: SubPoints-style fill between the two output endpoints. One per output facade.
		TSharedPtr<FPCGExSubPointsBlendOperation> SubBlending;
		TArray<TSharedPtr<FPCGExSubPointsBlendOperation>> SubBlendings;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool IsTrivial() const override
		{
			return false;
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;
	};
}
