// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExPointFilter.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExBitmaskFilter.generated.h"

class UPCGSettings;
class UPCGNode;


USTRUCT(BlueprintType)
struct PCGEXFILTERS_API FPCGExBitmaskFilterConfig
{
	GENERATED_BODY()

	FPCGExBitmaskFilterConfig()
	{
	}

	/** Source value. (Operand A) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName FlagsAttribute = FName("Flags");

	/** Type of flag comparison */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExBitflagComparison Comparison = EPCGExBitflagComparison::MatchPartial;

	/** Mask for testing -- Must be int64. (Operand B) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Bitmask"))
	FPCGExInputShorthandNameInteger64 BitmaskValue = FPCGExInputShorthandNameInteger64(FName("Mask"), 0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType MaskInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName BitmaskAttribute_DEPRECATED = FName("Mask");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int64 Bitmask_DEPRECATED = 0;

#pragma endregion

	/** External compositions applied to Operand B (whether it's a constant or not) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	TArray<FPCGExBitmaskRef> Compositions;

	/** Invert the filter result. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvertResult = false;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExBitmaskFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExBitmaskFilterConfig Config;

	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributes(FPCGExContext* InContext) const override;
};

namespace PCGExPointFilter
{
	class FBitmaskFilter final : public ISimpleFilter
	{
	public:
		explicit FBitmaskFilter(const TObjectPtr<const UPCGExBitmaskFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
			  , Bitmask(InDefinition->Config.BitmaskValue.Constant)
		{
		}

		TObjectPtr<const UPCGExBitmaskFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExData::TBuffer<int64>> FlagsReader;
		TSharedPtr<PCGExDetails::TSettingValue<int64>> MaskReader;

		int64 Bitmask;
		TArray<FPCGExSimpleBitmask> Compositions;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FBitmaskFilter() override
		{
			TypedFilterFactory = nullptr;
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/math/filter-bitmask"))
class UPCGExBitmaskFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(BitmaskFilterFactory, "Filter : Bitmask", "Filter using bitflag comparison.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExBitmaskFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
