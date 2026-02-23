// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PCGExMatchingCommon.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExMatchingDetails.h"

#include "PCGExDataMatchFilter.generated.h"

namespace PCGExMatching
{
	class FDataMatcher;
}

namespace PCGExData
{
	class FFacade;
}

USTRUCT(BlueprintType)
struct FPCGExDataMatchFilterConfig
{
	GENERATED_BODY()

	FPCGExDataMatchFilterConfig()
	{
	}

	/** How match rules are combined. All = every rule must pass. Any = at least one rule must pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExMapMatchMode Mode = EPCGExMapMatchMode::All;

	/** Invert the result of this filter. When inverted, collections that match will fail the filter instead of passing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bInvert = false;
};


/**
 *
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExDataMatchFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExDataMatchFilterConfig Config;

	UPROPERTY()
	FPCGExMatchingDetails MatchingDetails;

	TArray<TSharedRef<PCGExData::FFacade>> TargetFacades;
	TSharedPtr<PCGExMatching::FDataMatcher> DataMatcher;

	virtual bool SupportsCollectionEvaluation() const override { return true; }

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override { return true; }
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual void BeginDestroy() override;
};

namespace PCGExPointFilter
{
	class FDataMatchFilter final : public ISimpleFilter
	{
	public:
		explicit FDataMatchFilter(const TObjectPtr<const UPCGExDataMatchFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition), TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExDataMatchFilterFactory> TypedFilterFactory;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FDataMatchFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/data/filter-data-match"))
class UPCGExDataMatchFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(DataMatchFilterFactory, "Filter : Data Match", "Tests input data against targets using match rules.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExDataMatchFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual bool ShowMissingDataPolicy_Internal() const override { return true; }
#endif

protected:
	virtual bool IsCacheable() const override { return false; }
};
