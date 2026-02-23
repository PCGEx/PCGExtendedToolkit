// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Utils/PCGExCompare.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"
#include "PCGExPolyPathFilterFactory.h"
#include "Data/PCGExTaggedData.h"

#include "PCGExTimeFilter.generated.h"

namespace PCGExMatching
{
	class FDataMatcher;
}

UENUM()
enum class EPCGExSplineTimeConsolidation : uint8
{
	Min     = 0 UMETA(DisplayName = "Min", Tooltip="..."),
	Max     = 1 UMETA(DisplayName = "Max", Tooltip="..."),
	Average = 2 UMETA(DisplayName = "Average", Tooltip="...")
};

USTRUCT(BlueprintType)
struct FPCGExTimeFilterConfig
{
	GENERATED_BODY()

	FPCGExTimeFilterConfig() = default;

	/** Sample inputs.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSplineSamplingIncludeMode SampleInputs = EPCGExSplineSamplingIncludeMode::All;

	/** If a point is both inside and outside a spline (if there are multiple ones), decide what value to favor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExSplineFilterPick Pick = EPCGExSplineFilterPick::Closest;

	/** How to consolidate time values when multiple paths match. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Pick != EPCGExSplineFilterPick::Closest", EditConditionHides))
	EPCGExSplineTimeConsolidation TimeConsolidation = EPCGExSplineTimeConsolidation::Min;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

	/** Type of OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Operand B for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector OperandB;

	/** Operand B for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	float OperandBConstant = 0;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	PCGEX_SETTING_VALUE_DECL(OperandB, float)


	/** If enabled, invert the result of the test */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** Lets you enforce a path winding for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable), AdvancedDisplay)
	EPCGExWindingMutation WindingMutation = EPCGExWindingMutation::Unchanged;

	/** When projecting, defines the resolution of the polygon created from the spline. Lower values means higher fidelity, but slower execution. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, ClampMin=1), AdvancedDisplay)
	double Fidelity = 50;

	/** If enabled, when used with a collection filter, will use collection bounds as a proxy point instead of per-point testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bCheckAgainstDataBounds = false;

	/** If enabled, a collection will never be tested against itself */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Data matching settings. When enabled, only paths whose data matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFilterMatchingDetails DataMatching;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExTimeFilterFactory : public UPCGExPolyPathFilterFactory
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExTimeFilterConfig Config;

	virtual bool SupportsCollectionEvaluation() const override;
	virtual bool SupportsProxyEvaluation() const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

protected:
	virtual FName GetInputLabel() const override;
	virtual void InitConfig_Internal() override;
};

namespace PCGExPointFilter
{
	class FTimeFilter final : public ISimpleFilter
	{
	public:
		explicit FTimeFilter(const TObjectPtr<const UPCGExTimeFilterFactory>& InFactory)
			: ISimpleFilter(InFactory), TypedFilterFactory(InFactory)
		{
			Handler = TypedFilterFactory->CreateHandler();
		}

		const TObjectPtr<const UPCGExTimeFilterFactory> TypedFilterFactory;
		TSharedPtr<PCGExPathInclusion::FHandler> Handler;

		// Per-point matching -- see FDistanceFilter for full explanation.
		// Exclude set is used inline (not via FHandler) since Time checks MatchIgnoreList directly in its loops.
		TSharedPtr<PCGExMatching::FDataMatcher> InverseMatcher;
		bool bNoMatchResult = false;

		bool bCheckAgainstDataBounds = false;
		TConstPCGValueRange<FTransform> InTransforms;

		TSharedPtr<PCGExDetails::TSettingValue<float>> OperandB;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FTimeFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-time"))
class UPCGExTimeFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(TimeFilterFactory, "Filter : Time", "Creates a filter definition that checks points position against a path/spline/polygon2D closest alpha.", PCGEX_FACTORY_NAME_PRIORITY)
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExTimeFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual bool ShowMissingDataPolicy_Internal() const override { return true; }
#endif
};
