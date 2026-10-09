// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Factories/PCGExFactories.h"

#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Fitting/PCGExFittingCommon.h"
#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExTransformBounds.generated.h"

UENUM(BlueprintType)
enum class EPCGExBoundsVariationMode : uint8
{
	Offset = 0 UMETA(DisplayName = "Offset", ToolTip="Add a random offset to the point extents."),
	Scale  = 1 UMETA(DisplayName = "Scale", ToolTip="Multiply the point extents by a random factor."),
};

UENUM(BlueprintType)
enum class EPCGExBoundsPostMutation : uint8
{
	None          = 0 UMETA(DisplayName = "None", ToolTip="Leave point scale and bounds as they are."),
	ScaleToBounds = 1 UMETA(DisplayName = "Apply Scale to Bounds", ToolTip="Bake the point scale into its bounds so the scale becomes 1 on the selected axes."),
	BoundsToScale = 2 UMETA(DisplayName = "Apply Bounds to Scale", ToolTip="Set the local extents to a reference value and adapt the scale so the world-space bounds are unchanged on the selected axes."),
};

UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="transform/transform/transform-bounds"))
class UPCGExTransformBoundsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(TransformBounds, "Transform Bounds", "Randomly offset or scale point extents, with an optional exchange between point scale and bounds.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::PointOps;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Transform);
	}
#endif

	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filters", PCGExFactories::PointFilters, false)

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

public:
	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

#pragma region Extents

	/** Whether the random variation is added to the extents or multiplies them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable))
	EPCGExBoundsVariationMode Mode = EPCGExBoundsVariationMode::Offset;

	/** Minimum random extents offset per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Offset", EditConditionHides))
	FPCGExInputShorthandSelectorVector OffsetMin = FPCGExInputShorthandSelectorVector(FName("ExtentsOffsetMin"));

	/** Maximum random extents offset per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Offset", EditConditionHides))
	FPCGExInputShorthandSelectorVector OffsetMax = FPCGExInputShorthandSelectorVector(FName("ExtentsOffsetMax"));

	/** Scale applied to both Offset Min & Offset Max. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, DisplayName=" └─ Scaling", EditCondition="Mode == EPCGExBoundsVariationMode::Offset", EditConditionHides))
	FPCGExInputShorthandSelectorVector OffsetScaling = FPCGExInputShorthandSelectorVector(FName("Scaling"), FVector(1));

	/** How to snap the random offset. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Offset", EditConditionHides))
	EPCGExVariationSnapping SnapOffset = EPCGExVariationSnapping::None;

	/** Grid size for offset snapping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Offset && SnapOffset != EPCGExVariationSnapping::None", EditConditionHides))
	FPCGExInputShorthandSelectorVector OffsetSnap = FPCGExInputShorthandSelectorVector(FName("ExtentsStep"), FVector(100));

	/** Minimum random extents factor per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Scale", EditConditionHides))
	FPCGExInputShorthandSelectorVector ScaleMin = FPCGExInputShorthandSelectorVector(FName("ExtentsScaleMin"), FVector::OneVector);

	/** Maximum random extents factor per axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Scale", EditConditionHides))
	FPCGExInputShorthandSelectorVector ScaleMax = FPCGExInputShorthandSelectorVector(FName("ExtentsScaleMax"), FVector::OneVector);

	/** Scale applied to both Scale Min & Scale Max. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, DisplayName=" └─ Scaling", EditCondition="Mode == EPCGExBoundsVariationMode::Scale", EditConditionHides))
	FPCGExInputShorthandSelectorVector ScaleScaling = FPCGExInputShorthandSelectorVector(FName("Scaling"), FVector(1));

	/** Use the same factor on all axes (X component only). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Scale", EditConditionHides))
	FPCGExInputShorthandSelectorBoolean UniformScale = FPCGExInputShorthandSelectorBoolean(FName("UniformScale"), false, false);

	/** How to snap the random factor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Scale", EditConditionHides))
	EPCGExVariationSnapping SnapScale = EPCGExVariationSnapping::None;

	/** Increment for factor snapping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Extents", meta = (PCG_Overridable, EditCondition="Mode == EPCGExBoundsVariationMode::Scale && SnapScale != EPCGExVariationSnapping::None", EditConditionHides))
	FPCGExInputShorthandSelectorVector ScaleSnap = FPCGExInputShorthandSelectorVector(FName("ExtentsStep"), FVector(0.1));

#pragma endregion

#pragma region Post Mutation

	/** Exchange applied between point scale and bounds once the extents variation is done. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Post Mutation", meta = (PCG_NotOverridable))
	EPCGExBoundsPostMutation PostMutation = EPCGExBoundsPostMutation::None;

	/** Which axes the post mutation applies to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Post Mutation", meta=(PCG_NotOverridable, DisplayName=" └─ Components", EditCondition="PostMutation != EPCGExBoundsPostMutation::None", EditConditionHides, Bitmask, BitmaskEnum="/Script/PCGExBlending.EPCGExApplySampledComponentFlags"))
	uint8 Components = static_cast<uint8>(EPCGExApplySampledComponentFlags::All);

	/** Local extents set on the mutated axes; the point scale absorbs the difference. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Post Mutation", meta = (PCG_Overridable, DisplayName=" └─ Reference Extents", EditCondition="PostMutation == EPCGExBoundsPostMutation::BoundsToScale", EditConditionHides))
	FPCGExInputShorthandSelectorVector ReferenceExtents = FPCGExInputShorthandSelectorVector(FName("ReferenceExtents"), FVector(100));

#pragma endregion

private:
	friend class FPCGExTransformBoundsElement;
};

struct FPCGExTransformBoundsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExTransformBoundsElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExTransformBoundsElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(TransformBounds)

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExTransformBounds
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExTransformBoundsContext, UPCGExTransformBoundsSettings>
	{
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> OffsetMin;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> OffsetMax;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> OffsetScale;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> OffsetSnap;

		TSharedPtr<PCGExDetails::TSettingValue<FVector>> ScaleMin;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> ScaleMax;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> ScaleScale;
		TSharedPtr<PCGExDetails::TSettingValue<FVector>> ScaleSnap;
		TSharedPtr<PCGExDetails::TSettingValue<bool>> UniformScale;

		TSharedPtr<PCGExDetails::TSettingValue<FVector>> ReferenceExtents;

		bool bScaleMode = false;
		bool bScaleToBounds = false;
		bool bBoundsToScale = false;
		bool bAxes[3] = {false, false, false};

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
	};
}
