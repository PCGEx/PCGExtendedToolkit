// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExPointFilter.h"


#include "PCGExStringCompareFilter.generated.h"

namespace PCGExData
{
	template <typename T>
	class TAttributeBroadcaster;
}

USTRUCT(BlueprintType)
struct FPCGExStringCompareFilterConfig
{
	GENERATED_BODY()

	FPCGExStringCompareFilterConfig()
	{
	}

	/** Operand A for testing -- Read as text (string comparison). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName OperandA = NAME_None;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExStringComparison Comparison = EPCGExStringComparison::StrictlyEqual;

	/** Type of OperandB */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExInputValueType CompareAgainst = EPCGExInputValueType::Constant;

	/** Operand B for testing -- Read as text (string comparison). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B (Attr)", EditCondition="CompareAgainst != EPCGExInputValueType::Constant", EditConditionHides))
	FName OperandB = NAME_None;

	/** Operand B for testing */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Operand B", EditCondition="CompareAgainst == EPCGExInputValueType::Constant", EditConditionHides))
	FString OperandBConstant = TEXT("MyString");

	/** Swap operands. Useful to invert "contains" checks */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSwapOperands = false;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExStringCompareFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExStringCompareFilterConfig Config;

	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

namespace PCGExPointFilter
{
	class FStringCompareFilter final : public ISimpleFilter
	{
	public:
		explicit FStringCompareFilter(const TObjectPtr<const UPCGExStringCompareFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExStringCompareFilterFactory> TypedFilterFactory;

		// When the comparison is an equality check, operands are read as FName instead of
		// FString (cheaper; matches FString equality for typical values -- see
		// IsStringEqualityComparison for canonicalization caveats). Resolved once in Init from
		// Config.Comparison. Only one of the FString / FName operand pairs is ever populated.
		bool bUseNameComparison = false;

		TSharedPtr<PCGExData::TAttributeBroadcaster<FString>> OperandA;
		TSharedPtr<PCGExData::TAttributeBroadcaster<FString>> OperandB;

		TSharedPtr<PCGExData::TAttributeBroadcaster<FName>> OperandAName;
		TSharedPtr<PCGExData::TAttributeBroadcaster<FName>> OperandBName;
		FName OperandBConstantName = NAME_None;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FStringCompareFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-compare-string"))
class UPCGExStringCompareFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(StringCompareFilterFactory, "Filter : Compare (String)", "Creates a filter definition that compares two string attribute values.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** State name.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExStringCompareFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
