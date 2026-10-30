// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Fitting/PCGExFittingCommon.h"
#include "Math/PCGExMath.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "Utils/PCGExCurveLookup.h"

#include "PCGExRemapDetails.generated.h"

USTRUCT(BlueprintType)
struct PCGEXFOUNDATIONS_API FPCGExRemapDetails
{
	GENERATED_BODY()

	FPCGExRemapDetails()
	{
		LocalScoreCurve.EditorCurveData.AddKey(0, 0);
		LocalScoreCurve.EditorCurveData.AddKey(1, 1);
	}

	~FPCGExRemapDetails()
	{
	}

	/** Whether or not to use only positive values to compute range.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bUseAbsoluteRange = true;

	/** Whether or not to preserve value sign when using absolute range.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="bUseAbsoluteRange"))
	bool bPreserveSign = true;

	/** Fixed In Min value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bUseInMin = false;

	/** Fixed In Min value. If disabled, will use the lowest input value.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bUseInMin"))
	double InMin = 0;

	/** Fixed In Max value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle, FullyExpand=true))
	bool bUseInMax = false;

	/** Fixed In Max value. If disabled, will use the highest input value.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bUseInMax"))
	double InMax = 1;

	/** How to remap before sampling the curve. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExRangeType RangeMethod = EPCGExRangeType::EffectiveRange;

	/** Scale output value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Scale = 1;

	/** Whether to use in-editor curve or an external asset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bUseLocalCurve = false;

	// TODO: DirtyCache for OnDependencyChanged when this float curve is an external asset
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, DisplayName="Remap Curve", EditCondition = "bUseLocalCurve", EditConditionHides))
	FRuntimeFloatCurve LocalScoreCurve;

	UPROPERTY(EditAnywhere, Category = Settings, BlueprintReadWrite, meta =(PCG_Overridable, DisplayName="Remap Curve", EditCondition = "!bUseLocalCurve", EditConditionHides))
	TObjectPtr<UCurveFloat> RemapCurve = PCGExCurves::GetDefaultCurve(PCGExCurves::WeightDistributionLinear);

	PCGExFloatLUT RemapLUT = nullptr;

	// Resolved input remap target range, cached by Init(): [0,1] by default, or the
	// curve's control-point x-range when bRemapToCurveRange is set.
	double RemapRangeMin = 0;
	double RemapRangeMax = 1;

	/** Settings for how the curve is sampled (resolution, caching). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExCurveLookupDetails RemapCurveLookup;

	/** If enabled, the input is remapped into the curve's control-point x-range (min/max of the curve keys on the x-axis) instead of the normalized [0,1] range, so the full curve domain is sampled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bRemapToCurveRange = false;

	/** Whether and how to truncate output value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExTruncateMode TruncateOutput = EPCGExTruncateMode::None;

	/** Scale the value after it's been truncated. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="TruncateOutput != EPCGExTruncateMode::None", EditConditionHides))
	double PostTruncateScale = 1;

	/** Offset applied to the component after remap. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Offset = 0;

	/** How to snap the output value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExVariationSnapping Snapping = EPCGExVariationSnapping::None;

	/** Increment for value snapping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Snapping != EPCGExVariationSnapping::None", EditConditionHides))
	FPCGExInputShorthandSelectorDouble Snap = FPCGExInputShorthandSelectorDouble(FName("Step"), 10, false);

	void Init();

	double GetRemappedValue(const double Value, const double Step) const;
};
