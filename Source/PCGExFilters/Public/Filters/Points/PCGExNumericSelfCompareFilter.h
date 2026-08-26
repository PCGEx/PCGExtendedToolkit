// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Math/PCGExMath.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "PCGExNumericSelfCompareFilter.generated.h"

namespace PCGExData
{
	template <typename T>
	class TAttributeBroadcaster;
}

USTRUCT(BlueprintType)
struct FPCGExNumericSelfCompareFilterConfig
{
	GENERATED_BODY()

	FPCGExNumericSelfCompareFilterConfig() = default;

	/** Operand A for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** Index mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexMode IndexMode = EPCGExIndexMode::Offset;

	/** Whether the compared index is a constant or read from an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Index value to use according to the selected Index Mode -- Will be translated to `int32` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Index (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector IndexAttribute;

	/** Const Index value to use according to the selected Index Mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Index", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	int32 IndexConstant = -1;

	/** Index safety */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExIndexSafety IndexSafety = EPCGExIndexSafety::Clamp;

	PCGEX_SETTING_VALUE_DECL(Index, int32)

	/** How to deal with invalid indices */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFilterFallback InvalidIndexFallback = EPCGExFilterFallback::Fail;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExNumericSelfCompareFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNumericSelfCompareFilterConfig Config;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

namespace PCGExPointFilter
{
	class FNumericSelfCompareFilter final : public ISimpleFilter
	{
	public:
		explicit FNumericSelfCompareFilter(const TObjectPtr<const UPCGExNumericSelfCompareFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExNumericSelfCompareFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExData::TAttributeBroadcaster<double>> OperandA;
		TSharedPtr<PCGExDetails::TSettingValue<int32>> Index;
		bool bOffset = false;
		int32 MaxIndex = 0;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;

		virtual ~FNumericSelfCompareFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-self-compare-numeric"))
class UPCGExNumericSelfCompareFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NumericSelfCompareFilterFactory, "Filter : Self Compare (Numeric)", "Creates a filter definition that compares an attribute numeric value against itself at another index.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNumericSelfCompareFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
