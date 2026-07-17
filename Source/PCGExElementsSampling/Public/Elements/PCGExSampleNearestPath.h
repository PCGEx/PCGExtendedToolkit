// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Factories/PCGExFactories.h"

#include "Curves/CurveFloat.h"
#include "Utils/PCGExCurveLookup.h"

#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExMatchingDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Math/PCGExMathAxis.h"
#include "Math/PCGExProjectionDetails.h"
#include "Sampling/PCGExApplySamplingDetails.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExSampleNearestPath.generated.h"

#define PCGEX_FOREACH_FIELD_NEARESTPATH(MACRO)\
MACRO(Success, bool, false)\
MACRO(Transform, FTransform, FTransform::Identity)\
MACRO(LookAtTransform, FTransform, FTransform::Identity)\
MACRO(Distance, double, 0)\
MACRO(SignedDistance, double, 0)\
MACRO(ComponentWiseDistance, FVector, FVector::ZeroVector)\
MACRO(Angle, double, 0)\
MACRO(Time, double, 0)\
MACRO(SegmentTime, double, 0)\
MACRO(NumInside, int32, 0)\
MACRO(NumSamples, int32, 0)\
MACRO(ClosedLoop, int32, false)

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExMatching
{
	class FTargetsHandler;
}

class UPCGExBlendOpFactory;

namespace PCGExBlending
{
	class IUnionBlender;
	class FUnionOpsManager;
	class FBlendOpsSchema;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

namespace PCGExPaths
{
	class FPolyPath;
}

class UPCGExPointFilterFactoryData;

UENUM()
enum class EPCGExPathSamplingIncludeMode : uint8
{
	All            = 0 UMETA(DisplayName = "All", ToolTip="Sample all inputs"),
	ClosedLoopOnly = 1 UMETA(DisplayName = "Closed loops only", ToolTip="Sample only closed loops"),
	OpenLoopsOnly  = 2 UMETA(DisplayName = "Open lines only", ToolTip="Sample only open lines"),
};

UENUM()
enum class EPCGExPathSampleAlphaMode : uint8
{
	Alpha    = 0 UMETA(DisplayName = "Alpha", ToolTip="0 - 1 value"),
	Time     = 1 UMETA(DisplayName = "Time", ToolTip="0 - N value, where N is the number of segments"),
	Distance = 2 UMETA(DisplayName = "Distance", ToolTip="Distance on the path to sample value at"),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="sampling/nearest/sample-nearest-path"))
class UPCGExSampleNearestPathSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExSampleNearestPathSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(SampleNearestPath, "Sample : Nearest Path", "Sample the nearest(s) paths.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Sampling);
	}
#endif

protected:
	virtual FName GetMainInputPin() const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

	//~Begin UPCGExPointsProcessorSettings
public:
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters", PCGExFactories::PointFilters(), false)
	//~End UPCGExPointsProcessorSettings

	/** If enabled, allows you to filter out which targets get sampled by which data */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Sampling);

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	//

	/** Sample inputs.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	EPCGExPathSamplingIncludeMode SampleInputs = EPCGExPathSamplingIncludeMode::All;

	/** Sampling method.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	EPCGExSampleMethod SampleMethod = EPCGExSampleMethod::WithinRange;

	/** Sort direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta = (PCG_Overridable, DisplayName=" └─ Sort direction", EditCondition="SampleMethod == EPCGExSampleMethod::BestCandidate", EditConditionHides))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

	/** If enabled, will always sample paths if the point lies inside, even if further away from the edges than the specified max range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	bool bAlwaysSampleWhenInside = true;

	/** If enabled, will only sample paths if the point lies inside */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	bool bOnlySampleWhenInside = true;

	/** If non-zero, will apply an offset (inset) to the data used for inclusion testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	double InclusionOffset = 0;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType RangeMinInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector RangeMinAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double RangeMin_DEPRECATED = 0;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType RangeMaxInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector RangeMaxAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double RangeMax_DEPRECATED = 300;

#pragma endregion

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDouble MinRange = FPCGExInputShorthandSelectorDouble(FName("RangeMin"), 0, false);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDouble MaxRange = FPCGExInputShorthandSelectorDouble(FName("RangeMax"), 5000, false);


	/** If the value is greater than 0, will do a rough vertical check as part of the projected inclusion. 0 is infinite. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable, ClampMin=0))
	double HeightInclusion = 0;


	/** Whether spline should be sampled at a specific alpha */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	bool bSampleSpecificAlpha = false;

	/** How to interpret the sample alpha value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable, DisplayName=" ├─ Mode", EditCondition="bSampleSpecificAlpha", EditConditionHides))
	EPCGExPathSampleAlphaMode SampleAlphaMode = EPCGExPathSampleAlphaMode::Alpha;

	/** Whether to wrap out of bounds value on closed loops. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable, DisplayName=" ├─ Wrap Closed Loops", EditCondition="bSampleSpecificAlpha", EditConditionHides))
	bool bWrapClosedLoopAlpha = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable, DisplayName=" └─ Sample Alpha", EditCondition="bSampleSpecificAlpha", EditConditionHides))
	FPCGExInputShorthandSelectorDouble SampleAlpha = FPCGExInputShorthandSelectorDouble(FName("SampleAlpha"), 0.5, false);


#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType SampleAlphaInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector SampleAlphaAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double SampleAlphaConstant_DEPRECATED = 0.5;

#pragma endregion

	/** Whether and how to apply sampled result directly (not mutually exclusive with output)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	FPCGExApplySamplingDetails ApplySampling;

	/** Distance method to be used for source points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	EPCGExDistance DistanceSettings = EPCGExDistance::Center;

	/** Weight method used for blending */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	EPCGExRangeType WeightMethod = EPCGExRangeType::FullRange;

	/** If enabled, will preserve the original point transform as base for weighting. Otherwise, use transform identity. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	bool bWeightFromOriginalTransform = true;

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve that balances weight over distance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta = (PCG_NotOverridable, DisplayName="Weight Over Distance", EditCondition = "bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalWeightOverDistance;

	/** Curve that balances weight over distance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable, EditCondition="!bUseLocalCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> WeightOverDistance;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails WeightCurveLookup;

	/** Write whether the sampling was sucessful or not to a boolean attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSuccess = false;

	/** Name of the 'boolean' attribute to write sampling success to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Success", PCG_Overridable, EditCondition="bWriteSuccess"))
	FName SuccessAttributeName = FName("bSamplingSuccess");

	/** Write the sampled transform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTransform = false;

	/** Name of the 'transform' attribute to write sampled Transform to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Transform", PCG_Overridable, EditCondition="bWriteTransform"))
	FName TransformAttributeName = FName("WeightedTransform");


	/** Write the sampled transform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteLookAtTransform = false;

	/** Name of the 'transform' attribute to write sampled Transform to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="LookAt", PCG_Overridable, EditCondition="bWriteLookAtTransform"))
	FName LookAtTransformAttributeName = FName("WeightedLookAt");

	/** The axis to align transform the look at vector to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Align"))
	EPCGExAxisAlign LookAtAxisAlign = EPCGExAxisAlign::Forward;

	/** Up vector source.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Use Up from..."))
	EPCGExSampleSource LookAtUpSelection = EPCGExSampleSource::Constant;

	/** The attribute or property on selected source to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Up Vector (Attr)", EditCondition="LookAtUpSelection == EPCGExSampleSource::Source", EditConditionHides))
	FPCGAttributePropertyInputSelector LookAtUpSource;

	/** The axis on the target to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Up Vector (Axis)", EditCondition="LookAtUpSelection == EPCGExSampleSource::Target", EditConditionHides))
	EPCGExAxis LookAtUpAxis = EPCGExAxis::Up;

	/** The constant to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Up Vector", EditCondition="LookAtUpSelection == EPCGExSampleSource::Constant", EditConditionHides))
	FVector LookAtUpConstant = FVector::UpVector;

	PCGEX_SETTING_VALUE_DECL(LookAtUp, FVector)

	/** Write the sampled distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDistance = false;

	/** Name of the 'double' attribute to write sampled distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Distance", PCG_Overridable, EditCondition="bWriteDistance"))
	FName DistanceAttributeName = FName("WeightedDistance");

	/** Whether to output normalized distance or not*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Normalized", EditCondition="bWriteDistance", EditConditionHides, HideEditConditionToggle))
	bool bOutputNormalizedDistance = false;

	/** Whether to do a OneMinus on the normalized distance value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" │ └─ OneMinus", EditCondition="bWriteDistance && bOutputNormalizedDistance", EditConditionHides, HideEditConditionToggle))
	bool bOutputOneMinusDistance = false;

	/** Scale factor applied to the distance output; allows to easily invert it using -1 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Scale", EditCondition="bWriteDistance", EditConditionHides, HideEditConditionToggle))
	double DistanceScale = 1;

	/** Write the sampled Signed distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSignedDistance = false;

	/** Name of the 'double' attribute to write sampled Signed distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="SignedDistance", PCG_Overridable, EditCondition="bWriteSignedDistance"))
	FName SignedDistanceAttributeName = FName("WeightedSignedDistance");

	/** Axis to use to calculate the distance' sign*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Axis", EditCondition="bWriteSignedDistance", EditConditionHides, HideEditConditionToggle))
	EPCGExAxis SignAxis = EPCGExAxis::Forward;

	/** Only sign the distance if at least one sampled spline is a bClosedLoop spline. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Only if Closed Path", EditCondition="bWriteSignedDistance && SampleInputs == EPCGExPathSamplingIncludeMode::All", EditConditionHides, HideEditConditionToggle))
	bool bOnlySignIfClosed = false;

	/** Scale factor applied to the signed distance output; allows to easily invert it using -1 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Scale", EditCondition="bWriteSignedDistance", EditConditionHides, HideEditConditionToggle))
	double SignedDistanceScale = 1;

	/** Write the sampled component-wise distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteComponentWiseDistance = false;

	/** Name of the 'FVector' attribute to write component-wise distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Component Wise Distance", PCG_Overridable, EditCondition="bWriteComponentWiseDistance"))
	FName ComponentWiseDistanceAttributeName = FName("CWDistance");

	/** Whether to output absolute or signed component wise distances */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Absolute", EditCondition="bWriteComponentWiseDistance", EditConditionHides, HideEditConditionToggle))
	bool bAbsoluteComponentWiseDistance = true;

	/** Write the sampled angle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteAngle = false;

	/** Name of the 'double' attribute to write sampled Signed distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Angle", PCG_Overridable, EditCondition="bWriteAngle"))
	FName AngleAttributeName = FName("WeightedAngle");

	/** Axis to use to calculate the angle*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" ├─ Axis", EditCondition="bWriteAngle", EditConditionHides, HideEditConditionToggle))
	EPCGExAxis AngleAxis = EPCGExAxis::Forward;

	/** Unit/range to output the angle to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Range", EditCondition="bWriteAngle", EditConditionHides, HideEditConditionToggle))
	EPCGExAngleRange AngleRange = EPCGExAngleRange::PIRadians;

	/** Write the sampled time (spline space). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteTime = false;

	/** Name of the 'double' attribute to write sampled spline Time to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Time", PCG_Overridable, EditCondition="bWriteTime"))
	FName TimeAttributeName = FName("WeightedTime");

	/** Write the sampled time (spline space). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSegmentTime = false;

	/** Name of the 'double' attribute to write sampled spline Time to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Segment Time", PCG_Overridable, EditCondition="bWriteSegmentTime"))
	FName SegmentTimeAttributeName = FName("WeightedSegmentTime");

	/** Write the inside/outside status of the point toward any sampled spline. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNumInside = false;

	/** Name of the 'int32' attribute to write the number of spline this point lies inside*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(DisplayName="NumInside", PCG_Overridable, EditCondition="bWriteNumInside"))
	FName NumInsideAttributeName = FName("NumInside");

	/** Only increment num inside count when comes from a bClosedLoop spline. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, DisplayName=" └─ Only if Closed Spline", EditCondition="bWriteNumInside && SampleInputs == EPCGExPathSamplingIncludeMode::All", EditConditionHides, HideEditConditionToggle))
	bool bOnlyIncrementInsideNumIfClosed = false;

	/** Write the sampled distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNumSamples = false;

	/** Name of the 'int32' attribute to write the number of sampled neighbors to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(DisplayName="NumSamples", PCG_Overridable, EditCondition="bWriteNumSamples"))
	FName NumSamplesAttributeName = FName("NumSamples");

	/** Write the whether the sampled spline is closed or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteClosedLoop = false;

	/** Name of the 'bool' attribute to write whether a closed spline was sampled or not.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta=(DisplayName="ClosedLoop", PCG_Overridable, EditCondition="bWriteClosedLoop"))
	FName ClosedLoopAttributeName = FName("ClosedLoop");


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasSuccesses = false;

	/** If enabled, add the specified tag to the output data if at least a single spline has been sampled.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasSuccesses"))
	FString HasSuccessesTag = TEXT("HasSuccesses");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoSuccesses = false;

	/** If enabled, add the specified tag to the output data if no spline was found within range.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoSuccesses"))
	FString HasNoSuccessesTag = TEXT("HasNoSuccesses");

	//

	/** If enabled, mark filtered out points as "failed". Otherwise, just skip the processing altogether. Only uncheck this if you want to ensure existing attribute values are preserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bProcessFilteredOutAsFails = true;

	/** If enabled, points that failed to sample anything will be pruned. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bPruneFailedSamples = false;

	/** Exclude self from sampling when source points are also paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bIgnoreSelf = true;
};

struct FPCGExSampleNearestPathContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleNearestPathElement;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

	// Blend configs resolved once in Boot and shared across processors -- per-processor
	// blender init only instantiates ops instead of re-enumerating target metadata.
	TSharedPtr<PCGExBlending::FBlendOpsSchema> BlendOpsSchema;

	FPCGExApplySamplingDetails ApplySampling;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
	int32 NumMaxTargets = 0;

	TArray<TSharedPtr<PCGExPaths::FPolyPath>> Paths;
	TArray<TSharedPtr<PCGExDetails::TSettingValue<FVector>>> TargetLookAtUpGetters;

	TSharedPtr<PCGExSorting::FSorter> Sorter;

	PCGExFloatLUT WeightCurve = nullptr;

	PCGEX_FOREACH_FIELD_NEARESTPATH(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleNearestPathElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleNearestPath)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleNearestPath
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleNearestPathContext, UPCGExSampleNearestPathSettings>
	{
		TSet<const UPCGData*> IgnoreList;
		TArray<int8> SamplingMask;

		TSharedPtr<PCGExDetails::TSettingValue<double>> RangeMinGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> RangeMaxGetter;

		TSharedPtr<PCGExDetails::TSettingValue<double>> SampleAlphaGetter;

		FVector SafeUpVector = FVector::UpVector;
		TSharedPtr<PCGExData::TBuffer<FVector>> LookAtUpGetter;

		int8 bAnySuccess = 0;

		TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxSampledDistanceScoped;
		double MaxSampledDistance = 0;

		TSharedPtr<PCGExBlending::FUnionOpsManager> UnionBlendOpsManager;
		TSharedPtr<PCGExBlending::IUnionBlender> DataBlender;

		bool bSingleSample = false;
		bool bClosestSample = false;
		bool bOnlySignIfClosed = false;
		bool bOnlyIncrementInsideNumIfClosed = false;

		PCGEX_FOREACH_FIELD_NEARESTPATH(PCGEX_OUTPUT_DECL)

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		void SamplingFailed(const int32 Index);
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void OnPointsProcessingComplete() override;

		virtual void CompleteWork() override;

		virtual void Cleanup() override;
	};
}
