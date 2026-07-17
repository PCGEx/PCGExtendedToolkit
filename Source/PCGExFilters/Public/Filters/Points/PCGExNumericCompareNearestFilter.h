// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Utils/PCGExCompare.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "Filters/Points/PCGExNearestFilter.h"

#include "PCGExNumericCompareNearestFilter.generated.h"


USTRUCT(BlueprintType)
struct FPCGExNumericCompareNearestFilterConfig : public FPCGExNearestFilterConfigBase
{
	GENERATED_BODY()

	FPCGExNumericCompareNearestFilterConfig() = default;

	/** Operand A for testing -- Will be translated to `double` under the hood; read from the target points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExComparison Comparison = EPCGExComparison::NearlyEqual;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType CompareAgainst_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector OperandB_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double OperandBConstant_DEPRECATED = 0;

#pragma endregion

	/** OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorDouble OperandBValue = FPCGExInputShorthandSelectorDouble(FName("OperandB"), 0, false);

	/** Near-equality tolerance */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;
};


/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExNumericCompareNearestFilterFactory : public UPCGExNearestFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNumericCompareNearestFilterConfig Config;

	// Per-target operand A buffers, indexed by FConstPoint::IO.
	TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<double>>>> OperandA;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

protected:
	virtual void RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool BuildTargetCaches(FPCGExContext* InContext) override;
};

namespace PCGExPointFilter
{
	class FNumericCompareNearestFilter final : public FNearestFilter
	{
	public:
		explicit FNumericCompareNearestFilter(const TObjectPtr<const UPCGExNumericCompareNearestFilterFactory>& InDefinition)
			: FNearestFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
			OperandA = TypedFilterFactory->OperandA;
		}

		const TObjectPtr<const UPCGExNumericCompareNearestFilterFactory> TypedFilterFactory;

		TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<double>>>> OperandA;
		TSharedPtr<PCGExDetails::TSettingValue<double>> OperandB;

		virtual bool Test(const int32 PointIndex) const override;

	protected:
		virtual bool InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

	public:
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
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
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

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
