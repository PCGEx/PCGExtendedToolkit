// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExBooleanCompareFilter.generated.h"


USTRUCT(BlueprintType)
struct FPCGExBooleanCompareFilterConfig
{
	GENERATED_BODY()

	FPCGExBooleanCompareFilterConfig()
	{
	}

	/** Operand A for testing -- Will be translated to `bool` under the hood (numeric values read true when > 0). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExEquality Comparison = EPCGExEquality::Equal;

	/** Type of OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Operand B for testing -- Will be translated to `bool` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector OperandB;

	/** Operand B for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	bool OperandBConstant = true;

	PCGEX_SETTING_VALUE_DECL(OperandB, bool)
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExBooleanCompareFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExBooleanCompareFilterConfig Config;

	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

namespace PCGExPointFilter
{
	class FBooleanCompareFilter final : public ISimpleFilter
	{
	public:
		explicit FBooleanCompareFilter(const TObjectPtr<const UPCGExBooleanCompareFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExBooleanCompareFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExData::TBuffer<bool>> OperandA;
		TSharedPtr<PCGExDetails::TSettingValue<bool>> OperandB;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FBooleanCompareFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-bool-compare"))
class UPCGExBooleanCompareFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(BoolCompareFilterFactory, "Filter : Bool Compare", "(bool) A == (bool) B", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExBooleanCompareFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
