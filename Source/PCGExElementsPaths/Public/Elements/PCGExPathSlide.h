// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExFilterCommon.h"
#include "Core/PCGExPathProcessor.h"
#include "Details/PCGExSettingsMacros.h"
#include "Factories/PCGExFactories.h"
#include "Core/PCGExFilterTypeSets.h"
#include "Math/PCGExMathMean.h"

#include "PCGExPathSlide.generated.h"

namespace PCGExPaths
{
	class FPath;
}

UENUM()
enum class EPCGExSlideMode : uint8
{
	Slide   = 0 UMETA(DisplayName = "Slide", ToolTip="Slide points and optional store the original position to an attribute"),
	Restore = 1 UMETA(DisplayName = "Restore", ToolTip="Restore the original position from an attribute and deletes it."),
};

UENUM()
enum class EPCGExSlideDirection : uint8
{
	Next     = 0 UMETA(DisplayName = "Next", ToolTip="Slide toward next point"),
	Previous = 1 UMETA(DisplayName = "Previous", ToolTip="Slide toward previous point"),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="paths/transform/path-slide"))
class UPCGExPathSlideSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathSlide, "Path : Slide", "Slide points of a path along the path, either toward the next or previous point");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourceFiltersLabel, "Filter which points are processed by the slide maths.", PCGExFactories::PointFilters(), false)
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Whether to slide or restore position */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSlideMode Mode = EPCGExSlideMode::Slide;

	/** Slide toward the next or previous point on the path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Mode != EPCGExSlideMode::Restore", EditConditionHides))
	EPCGExSlideDirection Direction = EPCGExSlideDirection::Next;

	/** Discrete means actual distance, relative means a percentage of the segment length */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExSlideMode::Restore", EditConditionHides))
	EPCGExMeanMeasure AmountMeasure = EPCGExMeanMeasure::Relative;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExSlideMode::Restore", EditConditionHides))
	EPCGExInputValueType SlideAmountInput = EPCGExInputValueType::Constant;

	/** Slide amount. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Slide Amount (Attr)", EditCondition="SlideAmountInput == EPCGExInputValueType::Attribute && Mode != EPCGExSlideMode::Restore", EditConditionHides))
	FPCGAttributePropertyInputSelector SlideAmountAttribute;

	/** Slide amount. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Slide Amount", EditCondition="SlideAmountInput == EPCGExInputValueType::Constant && Mode != EPCGExSlideMode::Restore", EditConditionHides))
	double SlideAmountConstant = 0.5;

	PCGEX_SETTING_VALUE_DECL(SlideAmount, double)

	/** Whether to store the old position */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExSlideMode::Restore", EditConditionHides))
	bool bWriteOldPosition = true;

	/** Attribute to write to or restore from */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWriteOldPosition || Mode == EPCGExSlideMode::Restore", EditConditionHides))
	FName RestorePositionAttributeName = FName("PreSlidePosition");
};

struct FPCGExPathSlideContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathSlideElement;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathSlideElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathSlide)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathSlide
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathSlideContext, UPCGExPathSlideSettings>
	{
		bool bClosedLoop = false;

		TSharedPtr<PCGExDetails::TSettingValue<double>> SlideAmountGetter;
		TSharedPtr<PCGExData::TBuffer<FVector>> RestorePositionBuffer;

		TSharedPtr<PCGExPaths::FPath> Path;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;
	};
}
