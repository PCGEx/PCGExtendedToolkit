// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExBlendingCommon.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Factories/PCGExFactories.h"
#include "UObject/Object.h"
#include "Utils/PCGExCurveLookup.h"


#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExDistancesDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "Math/PCGExMathAxis.h"
#include "Sampling/PCGExApplySamplingDetails.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "Sorting/PCGExSortingCommon.h"

#include "PCGExSampleNearestPoint.generated.h"

#define PCGEX_FOREACH_FIELD_NEARESTPOINT(MACRO)\
MACRO(Success, bool, false)\
MACRO(Transform, FTransform, FTransform::Identity)\
MACRO(LookAtTransform, FTransform, FTransform::Identity)\
MACRO(Distance, double, 0)\
MACRO(SignedDistance, double, 0)\
MACRO(ComponentWiseDistance, FVector, FVector::ZeroVector)\
MACRO(Angle, double, 0)\
MACRO(NumSamples, int32, 0)\
MACRO(SampledIndex, int32, -1)

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExMatching
{
	class FTargetsHandler;
}

class UPCGExBlendOpFactory;

namespace PCGExMT
{
	template <typename T>
	class TScopedNumericValue;
}

namespace PCGExBlending
{
	class IUnionBlender;
	class FUnionOpsManager;
	class FUnionBlender;
	class FBlendOpsSchema;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Sampling", meta=(PCGExNodeLibraryDoc="sampling/nearest/sample-nearest-point"))
class UPCGExSampleNearestPointSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExSampleNearestPointSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(SampleNearestPoint, "Sample : Nearest Point", "Sample nearest target points.");

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

	//

	/** Sampling method.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_Overridable))
	EPCGExSampleMethod SampleMethod = EPCGExSampleMethod::WithinRange;

	/** Sort direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta = (PCG_Overridable, DisplayName=" └─ Sort direction", EditCondition="SampleMethod == EPCGExSampleMethod::BestCandidate", EditConditionHides))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Ascending;

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


	/** Distance method to be used for source & target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable, EditCondition="WeightMode != EPCGExSampleWeightMode::Attribute", EditConditionHides))
	FPCGExDistanceDetails DistanceDetails;

	/** Which mode to use to compute weights. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	EPCGExSampleWeightMode WeightMode = EPCGExSampleWeightMode::Distance;

	/** Weight attribute to read on targets. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable, EditCondition="WeightMode != EPCGExSampleWeightMode::Distance", EditConditionHides))
	FPCGAttributePropertyInputSelector WeightAttribute;

	/** Weight method used for blending */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable))
	EPCGExRangeType WeightMethod = EPCGExRangeType::FullRange;

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable, EditCondition="WeightMode != EPCGExSampleWeightMode::Attribute", EditConditionHides))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	/** Curve that balances weight over distance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta = (PCG_NotOverridable, DisplayName="Weight Over Distance", EditCondition = "WeightMode != EPCGExSampleWeightMode::Attribute && bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalWeightOverDistance;

	/** Curve that balances weight over distance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_Overridable, EditCondition="WeightMode != EPCGExSampleWeightMode::Attribute && !bUseLocalCurve", EditConditionHides))
	TSoftObjectPtr<UCurveFloat> WeightOverDistance;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Weighting", meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails WeightCurveLookup;

	/** Whether and how to apply sampled result directly (not mutually exclusive with output)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sampling", meta=(PCG_NotOverridable))
	FPCGExApplySamplingDetails ApplySampling;

	/** How to blend data from sampled points */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable))
	EPCGExBlendingInterface BlendingInterface = EPCGExBlendingInterface::Individual;

	/** Attributes to sample from the targets */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable, EditCondition="BlendingInterface == EPCGExBlendingInterface::Monolithic", EditConditionHides))
	TMap<FName, EPCGExBlendingType> TargetAttributes;

	/** Write the sampled distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable, EditCondition="BlendingInterface == EPCGExBlendingInterface::Monolithic", EditConditionHides))
	bool bBlendPointProperties = false;

	/** The constant to use as Up vector for the look at transform.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending", meta=(PCG_Overridable, EditCondition="bBlendPointProperties && BlendingInterface == EPCGExBlendingInterface::Monolithic", EditConditionHides))
	FPCGExPropertiesBlendingDetails PointPropertiesBlendingSettings = FPCGExPropertiesBlendingDetails(EPCGExBlendingType::None);


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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, DisplayName=" └─ Up Vector (Attr)", EditCondition="LookAtUpSelection != EPCGExSampleSource::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector LookAtUpSource;

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

	/** Write the sampled distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNumSamples = false;

	/** Name of the 'int32' attribute to write the number of sampled neighbors to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="NumSamples", PCG_Overridable, EditCondition="bWriteNumSamples"))
	FName NumSamplesAttributeName = FName("NumSamples");

	/** Write the sampled index to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSampledIndex = false;

	/** Name of the 'int32' attribute to write the sampled index to. Will use the closest index when sampling multiple points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="SampledIndex", PCG_Overridable, EditCondition="bWriteSampledIndex"))
	FName SampledIndexAttributeName = FName("SampledIndex");

	//

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasSuccesses = false;

	/** If enabled, add the specified tag to the output data if at least a single point has been sampled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasSuccesses"))
	FString HasSuccessesTag = TEXT("HasSuccesses");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfHasNoSuccesses = false;

	/** If enabled, add the specified tag to the output data if no points were sampled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfHasNoSuccesses"))
	FString HasNoSuccessesTag = TEXT("HasNoSuccesses");

	//

	/** If enabled, mark filtered out points as "failed". Otherwise, just skip the processing altogether. Only uncheck this if you want to ensure existing attribute values are preserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bProcessFilteredOutAsFails = true;

	/** If enabled, points that failed to sample anything will be pruned. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bPruneFailedSamples = false;

	/** If enabled, source points won't sample themselves as targets. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	bool bIgnoreSelf = true;
};

struct FPCGExSampleNearestPointContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExSampleNearestPointElement;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

	// Blend configs resolved once in Boot and shared across processors -- per-processor
	// blender init only instantiates ops instead of re-enumerating target metadata.
	TSharedPtr<PCGExBlending::FBlendOpsSchema> BlendOpsSchema;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
	int32 NumMaxTargets = 0;

	TArray<TSharedPtr<PCGExData::TBuffer<double>>> TargetWeights;
	TArray<TSharedPtr<PCGExDetails::TSettingValue<FVector>>> TargetLookAtUpGetters;

	TSharedPtr<PCGExSorting::FSorter> Sorter;

	FPCGExApplySamplingDetails ApplySampling;

	PCGExFloatLUT WeightCurve = nullptr;

	PCGEX_FOREACH_FIELD_NEARESTPOINT(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExSampleNearestPointElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleNearestPoint)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleNearestPoint
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExSampleNearestPointContext, UPCGExSampleNearestPointSettings>
	{
		TArray<int8> SamplingMask;

		bool bSingleSample = false;

		TSharedPtr<PCGExDetails::TSettingValue<double>> RangeMinGetter;
		TSharedPtr<PCGExDetails::TSettingValue<double>> RangeMaxGetter;

		FVector SafeUpVector = FVector::UpVector;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> LookAtUpGetter;

		FPCGExBlendingDetails BlendingDetails;

		TSharedPtr<PCGExBlending::FUnionBlender> UnionBlender;
		TSharedPtr<PCGExBlending::FUnionOpsManager> UnionBlendOpsManager;
		TSharedPtr<PCGExBlending::IUnionBlender> DataBlender;

		TSet<const UPCGData*> IgnoreList;
		TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxSampledDistanceScoped;
		double MaxSampledDistance = 0;

		int8 bAnySuccess = 0;

		PCGEX_FOREACH_FIELD_NEARESTPOINT(PCGEX_OUTPUT_DECL)

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
			DefaultPointFilterValue = true;
		}

		virtual ~FProcessor() override;

		void SamplingFailed(const int32 Index);

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void OnPointsProcessingComplete() override;

		virtual void CompleteWork() override;

		virtual void Cleanup() override;
	};
}
