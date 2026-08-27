// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPathProcessor.h"
#include "Paths/PCGExPathsCommon.h"
#include "Sampling/PCGExSamplingCommon.h"

#include "PCGExAttributeRolling.generated.h"

#define PCGEX_FOREACH_FIELD_ATTRIBUTE_ROLL(MACRO)\
MACRO(RangeStart, bool, false)\
MACRO(RangeStop, bool, false)\
MACRO(RangePole, bool, false)\
MACRO(IsInsideRange, bool, false)\
MACRO(RangeIndex, int32, -1)\
MACRO(IndexInsideRange, int32, 0)

class UPCGExBlendOpFactory;

namespace PCGExBlending
{
	class FBlendOpsManager;
}

UENUM()
enum class EPCGExRollingRangeControl : uint8
{
	StartStop = 0 UMETA(DisplayName = "Start/Stop", ToolTip="Uses two separate set of filters to start & stop rolling"),
	Toggle    = 1 UMETA(DisplayName = "Toggle", ToolTip="Uses a single set of filter that switches roll on/off whenever a point passes"),
};

UENUM()
enum class EPCGExRollingToggleInitialValue : uint8
{
	Constant         = 0 UMETA(DisplayName = "Constant", ToolTip="Use a constant value."),
	ConstantPreserve = 1 UMETA(DisplayName = "Constant (Preserve)", ToolTip="Use a constant value, but does not switch if the first value is the same."),
	FromPoint        = 2 UMETA(DisplayName = "From Point", ToolTip="Use the first point starting value.")
};

UENUM()
enum class EPCGExRollingValueControl : uint8
{
	Pin        = 0 UMETA(DisplayName = "Pin", ToolTip="Uses filter to determine when a point should be used as reference for rolling"),
	Previous   = 1 UMETA(DisplayName = "Previous", ToolTip="Use the previous point' value"),
	RangeStart = 2 UMETA(DisplayName = "Range Start", ToolTip="Use the first point of a range"),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Paths", meta=(Keywords = "range", PCGExNodeLibraryDoc="paths/modify/path-attribute-rolling"))
class UPCGExAttributeRollingSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExAttributeRollingSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(AttributeRolling, "Path : Attribute Rolling", "Does a rolling blending of properties & attributes.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual PCGExData::EIOInit GetMainDataInitializationPolicy() const override;

public:
	/** Rolling range control */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExRollingRangeControl RangeControl = EPCGExRollingRangeControl::Toggle;

	/** Rolling value control */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExRollingValueControl ValueControl = EPCGExRollingValueControl::Previous;

	/** How the initial rolling value is determined. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExRollingToggleInitialValue InitialValueMode = EPCGExRollingToggleInitialValue::Constant;

	/** Starting toggle value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="InitialValueMode != EPCGExRollingToggleInitialValue::FromPoint"))
	bool bInitialValue = true;

	/** Reverse rolling order */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bReverseRolling = false;

	/** Enable blend operations to be processed outside the rolling range. This can be useful in some cases. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bBlendOutsideRange = false;

	/** Include the stop element in blending. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="!bBlendOutsideRange"))
	bool bBlendStopElement = false;

	/** Write the range start to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRangeStart = false;

	/** Name of the 'bool' attribute to write range start to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Range Start", PCG_Overridable, EditCondition="bWriteRangeStart"))
	FName RangeStartAttributeName = FName("RangeStart");

	/** Write the range stop to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRangeStop = false;

	/** Name of the 'bool' attribute to write range stop to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Range Stop", PCG_Overridable, EditCondition="bWriteRangeStop"))
	FName RangeStopAttributeName = FName("RangeStop");

	/** Write the range pole to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRangePole = false;

	/** Name of the 'bool' attribute to write range pole to. A pole is either start or stop. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Range Pole", PCG_Overridable, EditCondition="bWriteRangePole"))
	FName RangePoleAttributeName = FName("RangePole");

	/** Write the range index to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteRangeIndex = false;

	/** Name of the 'int32' attribute to write range index to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Range Index", PCG_Overridable, EditCondition="bWriteRangeIndex"))
	FName RangeIndexAttributeName = FName("RangeIndex");

	/** Let you add an offset to the range index value. Since it's an index, its default value is -1, and the first index is 0; a default value of 0 or above may be more desirable for some usecases. */
	UPROPERTY(BlueprintReadWrite, Category = "Settings|Output", EditAnywhere, meta = (PCG_Overridable, DisplayName=" └─ Index Offset", EditCondition="bWriteRangeIndex", HideEditConditionToggle))
	int32 RangeIndexOffset = 0;

	/** Write whether the point is inside a range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIsInsideRange = false;

	/** Name of the 'bool' attribute to write whether a point is inside the range or not.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Is Inside Range", PCG_Overridable, EditCondition="bWriteIsInsideRange"))
	FName IsInsideRangeAttributeName = FName("IsInsideRange");


	/** Write the index inside range to an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteIndexInsideRange = false;

	/** Name of the 'int32' attribute to write range index to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(DisplayName="Index inside Range", PCG_Overridable, EditCondition="bWriteIndexInsideRange"))
	FName IndexInsideRangeAttributeName = FName("IndexInsideRange");
};

struct FPCGExAttributeRollingContext final : FPCGExPathProcessorContext
{
	friend class FPCGExAttributeRollingElement;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> PinFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> StartFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> StopFilterFactories;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;

	PCGEX_FOREACH_FIELD_ATTRIBUTE_ROLL(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExAttributeRollingElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(AttributeRolling)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExAttributeRolling
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExAttributeRollingContext, UPCGExAttributeRollingSettings>
	{
		int32 MaxIndex = 0;
		int32 SourceIndex = -1;
		int32 FirstIndex = -1;

		int32 SourceOffset = -1;

		bool bRoll = false;
		int32 RangeIndex = 0;
		int32 InternalRangeIndex = -1;

		TSharedPtr<PCGExPointFilter::FManager> PinFilterManager;
		TSharedPtr<PCGExPointFilter::FManager> StartFilterManager;
		TSharedPtr<PCGExPointFilter::FManager> StopFilterManager;

		PCGExPaths::FPathMetrics CurrentMetric;

		TSharedPtr<PCGExBlending::FBlendOpsManager> BlendOpsManager;

		PCGEX_FOREACH_FIELD_ATTRIBUTE_ROLL(PCGEX_OUTPUT_DECL)

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;
		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
	};
}
