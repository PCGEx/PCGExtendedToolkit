// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Utils/PCGExCompare.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExDistancesDetails.h"
#include "PCGExFilterCommon.h"
#include "Data/PCGExTaggedData.h"
#include "PCGExMatching/Public/Core/PCGExMatchRuleFactoryProvider.h"

#include "PCGExDistanceFilter.generated.h"


namespace PCGExMatching
{
	class FTargetsHandler;
	class FDataMatcher;
}

USTRUCT(BlueprintType)
struct FPCGExDistanceFilterConfig
{
	GENERATED_BODY()

	FPCGExDistanceFilterConfig()
	{
	}

	/** Distance method to be used for source & target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExDistanceDetails DistanceDetails;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

	/** Type of OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Operand B for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Distance Threshold (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector DistanceThreshold;

	/** Operand B for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Distance Threshold", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	double DistanceThresholdConstant = 0;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** If enabled, a collection will never be tested against itself */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bIgnoreSelf = false;

	/** Data matching settings. When enabled, only targets whose data matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFilterMatchingDetails DataMatching;

	PCGEX_SETTING_VALUE_DECL(DistanceThreshold, double)

	/** If enabled, when used with a collection filter, will use collection bounds as a proxy point instead of per-point testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bCheckAgainstDataBounds = false;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExDistanceFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExDistanceFilterConfig Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>> MatchRuleFactories;

	TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;

	virtual bool Init(FPCGExContext* InContext) override;

	virtual bool SupportsCollectionEvaluation() const override { return Config.bCheckAgainstDataBounds; }
	virtual bool SupportsProxyEvaluation() const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override { return true; }
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual void BeginDestroy() override;
};

namespace PCGExPointFilter
{
	class FDistanceFilter final : public ISimpleFilter
	{
	public:
		explicit FDistanceFilter(const TObjectPtr<const UPCGExDistanceFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition), TypedFilterFactory(InDefinition)
		{
			TargetsHandler = TypedFilterFactory->TargetsHandler;
		}

		const TObjectPtr<const UPCGExDistanceFilterFactory> TypedFilterFactory;
		TSharedPtr<PCGExMatching::FTargetsHandler> TargetsHandler;

		// Static matching: IgnoreList is built once in Init() using PopulateIgnoreListInverse (data-vs-data, uses first point only).
		// Used when bCheckAgainstDataBounds=true (collection-level proxy) or when matching is disabled (self-ignore only).
		TSet<const UPCGData*> IgnoreList;

		// Per-point matching: InverseMatcher + TargetCandidates are stored in Init(), then each Test(PointIndex)
		// builds a point-specific exclude set by testing that point's attributes against each candidate.
		// This is the correct path for per-point evaluation with attribute-based match rules (e.g. MatchTagToAttr).
		TSharedPtr<PCGExMatching::FDataMatcher> InverseMatcher;
		TArray<FPCGExTaggedData> TargetCandidates;
		bool bNoMatchResult = false;

		bool bCheckAgainstDataBounds = false;

		TSharedPtr<PCGExDetails::TSettingValue<double>> DistanceThresholdGetter;

		TConstPCGValueRange<FTransform> InTransforms;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FDistanceFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-distance"))
class UPCGExDistanceFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(DistanceFilterFactory, "Filter : Distance", "Creates a filter definition that compares the distance from the point to the nearest target.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExDistanceFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual bool ShowMissingDataPolicy_Internal() const override { return true; }
#endif

protected:
	virtual bool IsCacheable() const override { return false; }
};
