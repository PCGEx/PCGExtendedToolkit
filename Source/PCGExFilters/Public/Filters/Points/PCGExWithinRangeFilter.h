// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Pickers/PCGExPickerConstantRange.h"
#include "UObject/Object.h"

#include "PCGExWithinRangeFilter.generated.h"

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UENUM()
enum class EPCGExRangeSource : uint8
{
	Constant     = 0 UMETA(DisplayName = "Constant", ToolTip="Constant"),
	AttributeSet = 1 UMETA(DisplayName = "Attribute Set", ToolTip="Reading FVector2 attributes from an external attribute set"),
};

USTRUCT(BlueprintType)
struct FPCGExWithinRangeFilterConfig
{
	GENERATED_BODY()

	FPCGExWithinRangeFilterConfig()
	{
	}

	/** Operand A for testing -- Will be translated to `double` under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Where to read ranges from */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExRangeSource Source = EPCGExRangeSource::Constant;

	/** List of attributes to read ranges from FVector2. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Source == EPCGExRangeSource::AttributeSet", EditConditionHides))
	TArray<FPCGAttributePropertyInputSelector> Attributes;

	/** Range min value. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Source == EPCGExRangeSource::Constant", EditConditionHides))
	double RangeMin = -100;

	/** Range max value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Source == EPCGExRangeSource::Constant", EditConditionHides))
	double RangeMax = 100;

	/** Whether the max end of the range is inclusive. The min end is always included. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInclusive = false;

	/** If enabled, invert the result of the test and pass if value is outside the given range */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExWithinRangeFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExWithinRangeFilterConfig Config;

	TArray<FPCGExPickerConstantRangeConfig> Ranges;

	virtual bool DomainCheck() override;
	virtual bool Init(FPCGExContext* InContext) override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override;
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExPointFilter
{
	class FWithinRangeFilter final : public ISimpleFilter
	{
	public:
		explicit FWithinRangeFilter(const UPCGExWithinRangeFilterFactory* InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
			Ranges = InDefinition->Ranges;
		}

		const UPCGExWithinRangeFilterFactory* TypedFilterFactory;

		TSharedPtr<PCGExData::TBuffer<double>> OperandA;

		TArray<FPCGExPickerConstantRangeConfig> Ranges;

		bool bInclusive = false;
		bool bInvert = false;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FWithinRangeFilter() override
		{
			TypedFilterFactory = nullptr;
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-within-range"))
class UPCGExWithinRangeFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(RangeCompareFilterFactory, "Filter : Within Range", "Creates a filter definition check if a value is within a given range.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExWithinRangeFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return Config.Source == EPCGExRangeSource::AttributeSet;
	}
#endif
};
