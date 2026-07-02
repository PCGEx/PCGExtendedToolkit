// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExModuloCompareFilter.generated.h"


USTRUCT(BlueprintType)
struct FPCGExModuloCompareFilterConfig
{
	GENERATED_BODY()

	FPCGExModuloCompareFilterConfig() = default;

	/** Operand A for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Type of OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType OperandBSource = EPCGExInputValueType::Constant;

	/** Operand B for testing (Modulo base) -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B (Attr)", EditCondition="OperandBSource != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector OperandB;

	/** Operand B for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B", EditCondition="OperandBSource == EPCGExInputValueType::Constant", EditConditionHides))
	double OperandBConstant = 2;

	/** A static constant offset added to OperandB.
	 * Useful when dealing with angle or wider multples of (i.e 180d)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Offset"))
	double OperandBOffset = 0;
	
	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

	/** Type of OperandC */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Operand C for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, Displayname="Operand C (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector OperandC;

	/** Operand C for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand C", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	double OperandCConstant = 0;

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** Treat the modulo remainder as cyclic for ~= / ~!= : a remainder near 0 or near B both count as "near a multiple". No effect on ordering comparisons. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	bool bCyclicRemainder = true;

	/** Which value to return when dealing with zero-values */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool ZeroResult = true;

	PCGEX_SETTING_VALUE_DECL(OperandB, double)
	PCGEX_SETTING_VALUE_DECL(OperandC, double)
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExModuloCompareFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExModuloCompareFilterConfig Config;

	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

namespace PCGExPointFilter
{
	class FModuloComparisonFilter final : public ISimpleFilter
	{
	public:
		explicit FModuloComparisonFilter(const TObjectPtr<const UPCGExModuloCompareFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExModuloCompareFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExData::TBuffer<double>> OperandA;
		TSharedPtr<PCGExDetails::TSettingValue<double>> OperandB;
		TSharedPtr<PCGExDetails::TSettingValue<double>> OperandC;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FModuloComparisonFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/math/filter-modulo-compare"))
class UPCGExModuloCompareFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ModuloCompareFilterFactory, "Filter : Modulo Compare", "A % B != C", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExModuloCompareFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
