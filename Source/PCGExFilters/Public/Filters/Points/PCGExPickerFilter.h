// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExPickerFilter.generated.h"

class UPCGExPickerFactoryData;

USTRUCT(BlueprintType)
struct PCGEXFILTERS_API FPCGExPickerFilterConfig
{
	GENERATED_BODY()

	FPCGExPickerFilterConfig()
	{
	}

	/** Invert the filter */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExPickerFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExPickerFilterConfig Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPickerFactoryData>> PickerFactories;

	virtual bool SupportsCollectionEvaluation() const override
	{
		return true;
	}

	virtual bool Init(FPCGExContext* InContext) override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExPointFilter
{
	class FPickerFilter final : public ISimpleFilter
	{
	public:
		explicit FPickerFilter(const TObjectPtr<const UPCGExPickerFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExPickerFilterFactory> TypedFilterFactory;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FPickerFilter() override
		{
		}

	protected:
		TSet<int32> Picks;
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/filter-picker"))
class UPCGExPickerFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(PickerFilterFactory, "Filter : Picker", "Creates a filter definition that check if the point or collection index is picked, using pickers.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExPickerFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
