// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExDistancesDetails.h"
#include "PCGExFilterCommon.h"
#include "Data/PCGExTaggedData.h"
#include "PCGExMatching/Public/Core/PCGExMatchRuleFactoryProvider.h"

#include "PCGExNumericCompareNearestFilter.generated.h"


namespace PCGExMatching
{
	class FTargetsHandler;
	class FDataMatcher;
}

USTRUCT(BlueprintType)
struct FPCGExNumericCompareNearestFilterConfig
{
	GENERATED_BODY()

	FPCGExNumericCompareNearestFilterConfig() = default;

	/** Distance method to be used for source & target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExDistanceDetails DistanceDetails;

	/** Operand A for testing -- Will be translated to `double` under the hood; read from the target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

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
	double OperandBConstant = 0;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	PCGEX_SETTING_VALUE_DECL(OperandB, double)

	/** Exclude the point's own data from the nearest search. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Data matching settings. When enabled, only targets whose data matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFilterMatchingDetails DataMatching;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExNumericCompareNearestFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNumericCompareNearestFilterConfig Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>> MatchRuleFactories;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
	TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<double>>>> OperandA;

	virtual bool Init(FPCGExContext* InContext) override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override { return true; }
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual bool SupportsCollectionEvaluation() const override { return false; }

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
	virtual void BeginDestroy() override;
};

namespace PCGExPointFilter
{
	class FNumericCompareNearestFilter final : public ISimpleFilter
	{
	public:
		explicit FNumericCompareNearestFilter(const TObjectPtr<const UPCGExNumericCompareNearestFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition), TypedFilterFactory(InDefinition)
		{
			TargetsHandler = TypedFilterFactory->TargetsHandler;
			OperandA = TypedFilterFactory->OperandA;
		}

		const TObjectPtr<const UPCGExNumericCompareNearestFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;
		TSet<const UPCGData*> IgnoreList; // Self-ignore only (this filter has no static matching path)
		bool bMatchingFailed = false;

		// Per-point matching -- see FDistanceFilter for full explanation
		TSharedPtr<PCGExMatching::FDataMatcher> InverseMatcher;
		TArray<FPCGExTaggedData> TargetCandidates;
		bool bNoMatchResult = false;

		TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<double>>>> OperandA;

		TSharedPtr<PCGExDetails::TSettingValue<double>> OperandB;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;

		virtual ~FNumericCompareNearestFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-compare-nearest-numeric"))
class UPCGExNumericCompareNearestFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NumericCompareNearestFilterFactory, "Filter : Compare Nearest (Numeric)", "Creates a filter definition that compares two numeric attribute values.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNumericCompareNearestFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual bool ShowMissingDataPolicy_Internal() const override { return true; }
#endif
};
