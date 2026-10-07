// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Elements/PCGDataFromActor.h"
#include "Fitting/PCGExFitting.h"
#include "Filters/Points/PCGExPolyPathFilterFactory.h" // EPCGExSplineSamplingIncludeMode
#include "Details/PCGExFilterDetails.h"                 // EPCGExTagsToDataAction
#include "Math/PCGExMath.h"                             // EPCGExTruncateMode

#include "PCGExGetPathData.generated.h"

/** Get Path Data's Sample In Place settings. Extents are fallbacks, measured from the spline's centerline. */
USTRUCT(BlueprintType)
struct FPCGExSampleInPlaceDetails
{
	GENERATED_BODY()

	FPCGExSampleInPlaceDetails() = default;

	/** Desired sample length. Adjusted per path so the samples tile it exactly. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1"))
	double SampleLength = 100;

	/** How the fractional sample count (length / sample length) is rounded. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InvalidEnumValues = "None"))
	EPCGExTruncateMode Truncate = EPCGExTruncateMode::Round;

	/** Where the point sits inside its sample: 0 = start, 1 = end. Only the point moves, not its bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0", ClampMax = "1"))
	double Anchor = 0;

	/** Fallback extent on each side of the spline. Regular splines multiply it by their Y scale and the actor's
	 *  scale; types that store a width (rivers, landscape splines) use theirs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0"))
	double HalfWidth = 30;

	/** Fallback extent above the spline. Regular splines multiply it by their Z scale. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0"))
	double Above = 30;

	/** Fallback extent below the spline, times Z scale on regular splines. Water bodies that store a depth use it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0"))
	double Below = 0;

	/** Extend the width to soft edges where the spline type has them (landscape spline falloff). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIncludeFalloff = false;

	/** Miter the joints between samples. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bMiterJoints = false;

	/** When enabled, both boxes of a joint extend along the spline until their outer sides meet. The value caps
	 *  that extension as a multiple of the outer extent: 1 closes turns up to 90 degrees. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "bMiterJoints", DisplayName = "Miter Joints", ClampMin = "1", ClampMax = "100"))
	double MiterLimit = 1;
};

/**
 * Get Path Data
 *
 * Engine-style getter -- inherits UPCGDataFromActorSettings (NOT UPCGExSettings), so it runs on the
 * game thread and can read spline components directly off the selected actors, before they become PCG
 * data. Handles both USplineComponent and ULandscapeSplinesComponent via the shared UPCGPolyLineData
 * interface. Each spline is emitted as a PCGEx path (point data, closed-loop marked) and/or as the
 * source spline data, on separate pins. Convertible features are geometry-derived (transform, tangents,
 * length, alpha); point type is regular-spline-only (landscape splines have no CIM interp modes). In
 * exchange, every output is stamped with the source actor reference (@Data), which GetSplineData loses.
 *
 * Sample In Place swaps control points for evenly spaced samples holding the spline's footprint, read through the
 * spline sample handler registry (Helpers/PCGExSplineSampleHandler.h).
 *
 * Replaces the GetSplineData -> SplineToPath flow for the common "spline on an actor -> path/spline that
 * remembers its actor" case (e.g. feeding Get Properties Data).
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Path",
	meta = (Keywords = "pcgex spline path actor reference getter", PCGExNodeLibraryDoc = "paths/generate/get-path-data"))
class UPCGExGetPathDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetPathData")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGExGetPathData", "NodeTitle", "PCGEx | Get Path Data"); }
	virtual FText GetNodeTooltipText() const override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	// Fixed output pin types (Point + PolyLine), so opt out of the base's actor-data-driven dynamic typing.
	virtual bool HasDynamicPins() const override { return false; }

	// Output pins gray out (rather than vanish) when their toggle is off -- never drops downstream wires.
	virtual bool OutputPinsCanBeDeactivated() const override { return true; }
	virtual bool IsPinStaticallyActive(const FName& PinLabel) const override;
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGDataFromActorSettings interface
public:
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::PolyLine; }

protected:
#if WITH_EDITOR
	virtual bool DisplayModeSettings() const override { return false; }
#endif
	//~End UPCGDataFromActorSettings

public:
	/** Output a converted point path per spline (on the Paths pin). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_NotOverridable))
	bool bOutputPaths = true;

	/** Output the source spline data per spline on the Splines pin -- "GetSplineData that remembers its
	 *  actor" when Write Actor Reference is enabled. Built from the same component, so nearly free.
	 *  When sampling in place, only components that yield a sampled path are output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_NotOverridable))
	bool bOutputSplines = false;

	/** How point transforms inherit from the source spline. Location is always taken. Unused when sampling in place. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, EditCondition = "!bSampleInPlace"))
	FPCGExLeanTransformDetails TransformDetails;

	/** Which splines to include based on their open/closed state (per output path when sampling in place). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/** Sample each spline in place instead of emitting its control points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bSampleInPlace = false;

	/** When enabled, emits evenly spaced, scale-1 samples instead of control points. Each sample's bounds hold the
	 *  spline's footprint over the chord to the next sample, read the same way whatever the spline type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, EditCondition = "bSampleInPlace", DisplayName = "Sample In Place"))
	FPCGExSampleInPlaceDetails SampleInPlaceDetails;

	/** Write the spline arrive tangent to an attribute. Skipped when sampling in place. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, InlineEditConditionToggle, PCGExControlPointOnly))
	bool bWriteArriveTangent = true;

	/** Name of the 'FVector' attribute to write the arrive tangent to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (DisplayName = "Arrive Tangent", PCG_Overridable, EditCondition = "bWriteArriveTangent", PCGExControlPointOnly))
	FName ArriveTangentAttributeName = FName("ArriveTangent");

	/** Write the spline leave tangent to an attribute. Skipped when sampling in place. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, InlineEditConditionToggle, PCGExControlPointOnly))
	bool bWriteLeaveTangent = true;

	/** Name of the 'FVector' attribute to write the leave tangent to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (DisplayName = "Leave Tangent", PCG_Overridable, EditCondition = "bWriteLeaveTangent", PCGExControlPointOnly))
	FName LeaveTangentAttributeName = FName("LeaveTangent");

	/** Write the cumulative spline length at each point to an attribute (at the anchor when sampling in place). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLengthAtPoint = false;

	/** Name of the 'double' attribute to write the length at point to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (DisplayName = "Length at Point", PCG_Overridable, EditCondition = "bWriteLengthAtPoint"))
	FName LengthAtPointAttributeName = FName("LengthAtPoint");

	/** Write the normalized position along the spline (0-1) to an attribute (at the anchor when sampling in place). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAlpha = false;

	/** Name of the 'double' attribute to write the alpha to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (DisplayName = "Alpha", PCG_Overridable, EditCondition = "bWriteAlpha"))
	FName AlphaAttributeName = FName("Alpha");

	/** Write the original spline point type (Linear, Curve, etc.) to an attribute. Skipped when sampling in place. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (PCG_Overridable, InlineEditConditionToggle, PCGExControlPointOnly))
	bool bWritePointType = false;

	/** Name of the 'int32' attribute to write the point type to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Path", meta = (DisplayName = "Point Type", PCG_Overridable, EditCondition = "bWritePointType", PCGExControlPointOnly))
	FName PointTypeAttributeName = FName("PointType");

	/** Stamp each output path with the source actor reference, written to the @Data domain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteActorReference = true;

	/** Name of the 'FSoftObjectPath' @Data attribute to write the actor reference to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (DisplayName = "Actor Reference", PCG_Overridable, EditCondition = "bWriteActorReference"))
	FName ActorReferenceAttributeName = FName("ActorReference");

	/** Forward the source actor & spline-component tags onto the output data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging")
	bool bForwardSourceTags = true;

	/** How key:value tags (from the forwarded actor/component tags) are converted to attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta = (PCG_NotOverridable))
	EPCGExTagsToDataAction TagsToData = EPCGExTagsToDataAction::ToData;
};

class FPCGExGetPathDataElement : public FPCGDataFromActorElement
{
protected:
	// Drives both phases. NewObject_AnyThread + metadata setup happen single-threaded here; only the
	// per-point value fill is parallelized (NewObject_AnyThread is not safe across concurrent threads).
	virtual void ProcessActors(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, const TArray<AActor*>& FoundActors) const override;
};
