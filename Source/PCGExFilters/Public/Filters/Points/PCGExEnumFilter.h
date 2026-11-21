// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "PCGExEnumSelector.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "UObject/Object.h"

#include "PCGExEnumFilter.generated.h"

UENUM()
enum class EPCGExEnumFilterMode : uint8
{
	StrictlyEqual = 0 UMETA(DisplayName = "Strictly Equal", Tooltip="Attribute value must equal the selected enum value"),
	AnyOf         = 1 UMETA(DisplayName = "Any Of", Tooltip="Attribute value must be one of the checked enum values"),
};

USTRUCT(BlueprintType)
struct FPCGExEnumFilterConfig
{
	GENERATED_BODY()

	FPCGExEnumFilterConfig() = default;

	/** Attribute to test. Read as int64, so any numeric attribute holding an enum value works. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;

	/** Whether the attribute must match one selected value, or any of a checked set. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExEnumFilterMode Mode = EPCGExEnumFilterMode::StrictlyEqual;

	/** Enum class and value. In 'Any Of' mode only the class is used; the value comes from the checked set below. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	FPCGExEnumSelector Enum;

	/** Values that pass in 'Any Of' mode. Keys are rebuilt from the selected enum class; check the values to accept. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, EditFixedSize, meta=(PCG_NotOverridable, ReadOnlyKeys, EditCondition="Mode == EPCGExEnumFilterMode::AnyOf", EditConditionHides))
	TMap<FName, bool> Values;

	/** Runtime mirror of the checked Values, baked in-editor. Display names are not reliably resolvable in cooked builds. */
	UPROPERTY()
	TArray<int64> SelectedValues;

	/** If enabled, invert the result of the test. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

#if WITH_EDITOR
	/** Rebuilds Values against the current enum class (preserving existing checks) and re-bakes SelectedValues. */
	void SyncValues();
#endif
};

/**
 * Factory for enum filters: tests an int64-broadcast attribute against one enum value or a set of them.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExEnumFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExEnumFilterConfig Config;

	/** Bit i set means value i passes; covers 0..63. Values outside that range live in LargeValues, sorted. */
	uint64 ValueMask = 0;
	TArray<int64> LargeValues;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual bool DomainCheck() override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	FORCEINLINE bool Matches(const int64 Value) const
	{
		if (Config.Mode == EPCGExEnumFilterMode::StrictlyEqual)
		{
			return Value == Config.Enum.Value;
		}
		if (static_cast<uint64>(Value) < 64)
		{
			return (ValueMask & (1ull << Value)) != 0;
		}
		return !LargeValues.IsEmpty() && Algo::BinarySearch(LargeValues, Value) != INDEX_NONE;
	}
};

namespace PCGExPointFilter
{
	class FEnumFilter final : public ISimpleFilter
	{
	public:
		explicit FEnumFilter(const TObjectPtr<const UPCGExEnumFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExEnumFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExData::TBuffer<int64>> Reader;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/attribute/filter-enum"))
class UPCGExEnumFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UObject interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(EnumFilterFactory, "Filter : Enum", "Creates a filter definition that tests an attribute against enum values.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual TArray<FText> GetNodeTitleAliases() const override;
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExEnumFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
