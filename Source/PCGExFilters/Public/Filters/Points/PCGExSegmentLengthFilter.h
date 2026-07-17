// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Math/PCGExMath.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "PCGExSegmentLengthFilter.generated.h"

class UPCGSettings;
class UPCGNode;

USTRUCT(BlueprintType)
struct FPCGExSegmentLengthFilterConfig
{
	GENERATED_BODY()

	FPCGExSegmentLengthFilterConfig() = default;

	/** Constant threshold distance for comparison. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Threshold"))
	FPCGExInputShorthandSelectorDoubleAbs Threshold = FPCGExInputShorthandSelectorDoubleAbs(FString(TEXT("")), 100, false);

	/** If enabled, will compare against the squared distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Squared Distance"))
	bool bCompareAgainstSquaredDistance = false;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType ThresholdInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector ThresholdAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double ThresholdConstant_DEPRECATED = 100;

#pragma endregion

	/** Comparison check */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::StrictlyGreater;

	/** Rounding mode for approx. comparison modes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = 0;


	/** Index mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexMode IndexMode = EPCGExIndexMode::Offset;

	/** Type of OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Index value to use according to the selected Index Mode -- Will be translated to `int32` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Index (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector IndexAttribute;

	/** Const Index value to use according to the selected Index Mode, If offset mode, 1 would be next point, -1 previous point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Index", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	int32 IndexConstant = 1;

	/** Index safety */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Clamp;

	/** If enabled, will force Tile safety on closed loop paths */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, DisplayName=" └─ Tile on closed loops"))
	bool bForceTileIfClosedLoop = true;

	PCGEX_SETTING_VALUE_DECL(Index, int32)

	/** What should this filter return when the point required for computing length is invalid? (i.e, first or last point) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFilterFallback InvalidPointFallback = EPCGExFilterFallback::Fail;

	/** Whether the result of the filter should be inverted or not. Note that this will also invert fallback results! */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bInvert = false;

	void Sanitize()
	{
	}

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExSegmentLengthFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExSegmentLengthFilterConfig Config;

	virtual bool Init(FPCGExContext* InContext) override;

	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual bool SupportsCollectionEvaluation() const override
	{
		return false;
	}

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

namespace PCGExPointFilter
{
	class FSegmentLengthFilter final : public ISimpleFilter
	{
	public:
		explicit FSegmentLengthFilter(const TObjectPtr<const UPCGExSegmentLengthFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExSegmentLengthFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExDetails::TSettingValue<double>> Threshold;
		TSharedPtr<PCGExDetails::TSettingValue<int32>> Index;
		bool bOffset = false;

		bool bClosedLoop = false;
		int32 LastIndex = -1;

		TConstPCGValueRange<FTransform> InTransforms;

		EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Clamp;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;

		virtual ~FSegmentLengthFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/math/filter-segment-length"))
class UPCGExSegmentLengthFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(SegmentLengthFilterFactory, "Filter : Segment Length", "Creates a filter definition that compares the distance between the tested point and another inside the same dataset.")
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSegmentLengthFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
