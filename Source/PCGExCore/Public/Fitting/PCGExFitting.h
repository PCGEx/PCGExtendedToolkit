// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFittingCommon.h"
#include "PCGExFittingOverrides.h"
#include "Data/PCGExDataCommon.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExFitting.generated.h"

struct FPCGExFittingVariations;

namespace PCGExData
{
}

struct FPCGExContext;

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

namespace PCGExData
{
	struct FPoint;
	class FFacade;

	template <typename T>
	class TBuffer;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExScaleToFitDetails
{
	GENERATED_BODY()

	FPCGExScaleToFitDetails()
	{
	}

	explicit FPCGExScaleToFitDetails(const EPCGExFitMode DefaultFit)
	{
		ScaleToFitMode = DefaultFit;
	}

	/**
	 * How scaling is applied to fit within target bounds.
	 * None = no scaling, Uniform = same scale all axes, Individual = per-axis control.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFitMode ScaleToFitMode = EPCGExFitMode::Uniform;

	/**
	 * Uniform scaling strategy.
	 * Fill = stretch to fill, Min = fit smallest axis, Max = fit largest axis, Avg = average.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="ScaleToFitMode == EPCGExFitMode::Uniform", EditConditionHides))
	EPCGExScaleToFit ScaleToFit = EPCGExScaleToFit::Min;

	/** Scaling strategy for X axis when using Individual mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="ScaleToFitMode == EPCGExFitMode::Individual", EditConditionHides))
	EPCGExScaleToFit ScaleToFitX = EPCGExScaleToFit::None;

	/** Scaling strategy for Y axis when using Individual mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="ScaleToFitMode == EPCGExFitMode::Individual", EditConditionHides))
	EPCGExScaleToFit ScaleToFitY = EPCGExScaleToFit::None;

	/** Scaling strategy for Z axis when using Individual mode. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="ScaleToFitMode == EPCGExFitMode::Individual", EditConditionHides))
	EPCGExScaleToFit ScaleToFitZ = EPCGExScaleToFit::None;

	void Process(const PCGExData::FPoint& InPoint, const FBox& InBounds, FVector& OutScale, FBox& OutBounds) const;

	bool IsEnabled() const
	{
		if (ScaleToFitMode == EPCGExFitMode::None)
		{
			return false;
		}
		if (ScaleToFitMode == EPCGExFitMode::Uniform)
		{
			return ScaleToFit != EPCGExScaleToFit::None;
		}
		return !(ScaleToFitX == EPCGExScaleToFit::None
			&& ScaleToFitY == EPCGExScaleToFit::None
			&& ScaleToFitZ == EPCGExScaleToFit::None);
	}
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExSingleJustifyDetails
{
	GENERATED_BODY()

	FPCGExSingleJustifyDetails();

	/**
	 * Reference point on the object being positioned.
	 * Min/Center/Max/Pivot or Custom for attribute-driven.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExJustifyFrom From = EPCGExJustifyFrom::Center;

	/**
	 * Attribute for custom 'From' position.
	 * 0 = bounds min, 0.5 = center, 1 = bounds max.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Source", EditCondition="From == EPCGExJustifyFrom::Custom", EditConditionHides))
	FPCGExInputShorthandSelectorDouble CustomFrom = FPCGExInputShorthandSelectorDouble(FName("From"), 0.5, false);

	TSharedPtr<PCGExDetails::TSettingValue<double>> FromGetter;
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> SharedFromGetter;

	/**
	 * Target point in the container bounds to align to.
	 * Same = match 'From', or Min/Center/Max/Pivot/Custom.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExJustifyTo To = EPCGExJustifyTo::Same;

	/**
	 * Attribute for custom 'To' position.
	 * 0 = bounds min, 0.5 = center, 1 = bounds max.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Source", EditCondition="To == EPCGExJustifyTo::Custom", EditConditionHides))
	FPCGExInputShorthandSelectorDouble CustomTo = FPCGExInputShorthandSelectorDouble(FName("To"), 0.5, false);

	TSharedPtr<PCGExDetails::TSettingValue<double>> ToGetter;
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> SharedToGetter;

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade);

	void JustifyAxis(const int32 Axis, const int32 Index, const FVector& InCenter, const FVector& InSize, const FVector& OutCenter, const FVector& OutSize, FVector& OutTranslation) const;
};

USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/transform-details/justification-details"))
struct PCGEXCORE_API FPCGExJustificationDetails
{
	GENERATED_BODY()

	FPCGExJustificationDetails() = default;

	explicit FPCGExJustificationDetails(const bool bInEnabled)
		: bDoJustifyX(bInEnabled)
		  , bDoJustifyY(bInEnabled)
		  , bDoJustifyZ(bInEnabled)
	{
	}

	/** Enable justification on X axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bDoJustifyX = true;

	/** X axis justification settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bDoJustifyX"))
	FPCGExSingleJustifyDetails JustifyX;

	/** Enable justification on Y axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bDoJustifyY = true;

	/** Y axis justification settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bDoJustifyY"))
	FPCGExSingleJustifyDetails JustifyY;

	/** Enable justification on Z axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bDoJustifyZ = true;

	/** Z axis justification settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bDoJustifyZ"))
	FPCGExSingleJustifyDetails JustifyZ;

	/** Use a single FVector attribute for all 'From' positions instead of per-axis attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bSharedCustomFromAttribute = false;

	/**
	 * Vector attribute where X/Y/Z components provide 'From' positions
	 * for corresponding axes. Overrides per-axis attribute settings.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSharedCustomFromAttribute"))
	FPCGExInputShorthandSelectorVector CustomFrom = FPCGExInputShorthandSelectorVector(FName("From"), FVector(0.5), true);

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> SharedFromGetter;

	/** Use a single FVector attribute for all 'To' positions instead of per-axis attributes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bSharedCustomToAttribute = false;

	/**
	 * Vector attribute where X/Y/Z components provide 'To' positions
	 * for corresponding axes. Overrides per-axis attribute settings.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bSharedCustomToAttribute"))
	FPCGExInputShorthandSelectorVector CustomTo = FPCGExInputShorthandSelectorVector(FName("To"), FVector(0.5), true);

	TSharedPtr<PCGExDetails::TSettingValue<FVector>> SharedToGetter;

	void Process(const int32 Index, const FBox& InBounds, const FBox& OutBounds, FVector& OutTranslation) const;

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade);
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExFittingVariationsDetails
{
	GENERATED_BODY()

	FPCGExFittingVariationsDetails()
	{
	}

	/**
	 * When to apply random offset variation.
	 * Before = applied to asset before fitting, After = applied to result.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExVariationMode Offset = EPCGExVariationMode::Disabled;

	/**
	 * When to apply random rotation variation.
	 * Before = applied to asset before fitting, After = applied to result.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExVariationMode Rotation = EPCGExVariationMode::Disabled;

	/**
	 * When to apply random scale variation.
	 * Before = applied to asset before fitting, After = applied to result.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExVariationMode Scale = EPCGExVariationMode::Disabled;

	bool bEnabledBefore = true;
	bool bEnabledAfter = true;

	int Seed = 0;

	void Init(const int InSeed);

	void Apply(const FRandomStream& RandomStream, FTransform& OutTransform, const FPCGExFittingVariations& Variations, const EPCGExVariationMode& Step) const;
};


USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExFittingDetailsHandler
{
	GENERATED_BODY()

	explicit FPCGExFittingDetailsHandler()
	{
	}

	/** How to scale objects to fit within target bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties, AlwaysExpand=true))
	FPCGExScaleToFitDetails ScaleToFit;

	/** How to align objects within target bounds after scaling. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties, AlwaysExpand=true))
	FPCGExJustificationDetails Justification;

	TSharedPtr<PCGExData::FFacade> TargetDataFacade;

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InTargetFacade);

	/** InOverrides members, when set, replace the corresponding node-level details for this computation (per-entry overrides). */
	void ComputeTransform(const int32 TargetIndex, FTransform& OutTransform, FBox& InOutBounds, FVector& OutTranslation, const bool bWorldSpace = true, const PCGExFitting::FOverridesView& InOverrides = {}) const;
	void ComputeLocalTransform(const int32 TargetIndex, const FTransform& InLocalXForm, FTransform& OutTransform, FBox& InOutBounds, FVector& OutTranslation, const PCGExFitting::FOverridesView& InOverrides = {}) const;

	bool WillChangeBounds() const;
	bool WillChangeTransform() const;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExTransformDetails : public FPCGExFittingDetailsHandler
{
	GENERATED_BODY()

	explicit FPCGExTransformDetails()
	{
	}

	explicit FPCGExTransformDetails(const bool InInheritScale, const bool InInheritRotation)
		: bInheritScale(InInheritScale)
		  , bInheritRotation(InInheritRotation)
	{
	}

	/** Multiply result scale by the target point's scale. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayAfter="Justification"))
	bool bInheritScale = false;

	/** Rotate result by the target point's rotation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayAfter="bInheritScale"))
	bool bInheritRotation = false;

	/**
	 * Skip bounds calculations and use position only.
	 * Disables scale-to-fit and justification.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayAfter="bInheritRotation"))
	bool bIgnoreBounds = false;
};

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExLeanTransformDetails
{
	GENERATED_BODY()

	explicit FPCGExLeanTransformDetails()
	{
	}

	/** Multiply result scale by the parent's scale. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInheritScale = true;

	/** Rotate result by the parent's rotation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInheritRotation = true;

	/** Write Source's location into OutTransform, plus rotation/scale per the inherit flags. Components that
	 *  aren't inherited are left untouched (callers pass an identity-initialized transform). */
	void ApplyTo(FTransform& OutTransform, const FTransform& Source) const
	{
		if (bInheritRotation && bInheritScale)
		{
			OutTransform = Source;
		}
		else if (bInheritRotation)
		{
			OutTransform.SetLocation(Source.GetLocation());
			OutTransform.SetRotation(Source.GetRotation());
		}
		else if (bInheritScale)
		{
			OutTransform.SetLocation(Source.GetLocation());
			OutTransform.SetScale3D(Source.GetScale3D());
		}
		else
		{
			OutTransform.SetLocation(Source.GetLocation());
		}
	}
};
