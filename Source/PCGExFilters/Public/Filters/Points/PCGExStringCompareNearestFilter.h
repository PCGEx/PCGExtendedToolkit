// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Utils/PCGExCompare.h"

#include "Filters/Points/PCGExNearestFilter.h"

#include "PCGExStringCompareNearestFilter.generated.h"


USTRUCT(BlueprintType)
struct FPCGExStringCompareNearestFilterConfig : public FPCGExNearestFilterConfigBase
{
	GENERATED_BODY()

	FPCGExStringCompareNearestFilterConfig() = default;

	/** Operand A for testing -- read from the closest target point. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector OperandA;

	/** Comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExStringComparison Comparison = EPCGExStringComparison::StrictlyEqual;

	/** Operand B for testing -- read from the point being tested (source). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExInputShorthandSelectorString OperandBValue = FPCGExInputShorthandSelectorString(FName("OperandB"), FString(TEXT("MyString")));

	/** Swap operands. Useful to invert "contains" checks. Has no effect on equality comparisons. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSwapOperands = false;
};


/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExStringCompareNearestFilterFactory : public UPCGExNearestFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExStringCompareNearestFilterConfig Config;

	// Equality comparisons read operands as FName (cheaper; see IsStringEqualityComparison for caveats).
	bool bUseNameComparison = false;

	// Per-target operand A buffers. Only one is populated, depending on bUseNameComparison.
	TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<FString>>>> OperandAString;
	TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<FName>>>> OperandAName;
	// Raw input arrays of the populated table (fully read before built); null where the reader is missing or not Elements-domain.
	TSharedPtr<TArray<const FString*>> OperandAStringData;
	TSharedPtr<TArray<const FName*>> OperandANameData;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

protected:
	virtual void RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool BuildTargetCaches(FPCGExContext* InContext) override;
};

namespace PCGExPointFilter
{
	class FStringCompareNearestFilter final : public FNearestFilter
	{
	public:
		explicit FStringCompareNearestFilter(const TObjectPtr<const UPCGExStringCompareNearestFilterFactory>& InDefinition)
			: FNearestFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
			bUseNameComparison = TypedFilterFactory->bUseNameComparison;
			OperandAString = TypedFilterFactory->OperandAString;
			OperandAName = TypedFilterFactory->OperandAName;
			OperandAStringData = TypedFilterFactory->OperandAStringData;
			OperandANameData = TypedFilterFactory->OperandANameData;
		}

		const TObjectPtr<const UPCGExStringCompareNearestFilterFactory> TypedFilterFactory;

		bool bUseNameComparison = false;

		// Per-target operand A buffers and their raw input arrays (shared from factory). Only one pair is populated.
		TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<FString>>>> OperandAString;
		TSharedPtr<TArray<TSharedPtr<PCGExData::TBuffer<FName>>>> OperandAName;
		TSharedPtr<TArray<const FString*>> OperandAStringData;
		TSharedPtr<TArray<const FName*>> OperandANameData;

		// Operand B read from the source point. Only one is populated.
		TSharedPtr<PCGExDetails::TSettingValue<FString>> OperandBString;
		TSharedPtr<PCGExDetails::TSettingValue<FName>> OperandBName;

		virtual bool Test(const int32 PointIndex) const override;

	protected:
		virtual bool InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

	public:
		virtual ~FStringCompareNearestFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-compare-nearest-string"))
class UPCGExStringCompareNearestFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(StringCompareNearestFilterFactory, "Filter : Compare Nearest (String)", "Creates a filter definition that compares two string attribute values, reading operand A from the closest target.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExStringCompareNearestFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
