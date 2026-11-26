// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExMatchingCommon.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExMatchingDetails.h"
#include "HAL/CriticalSection.h"
#include "UObject/Object.h"

#include <atomic>

#include "PCGExDataMatchFilter.generated.h"

class UPCGExMatchRuleFactoryData;
struct FPCGExTaggedData;

namespace PCGExMatching
{
	class FDataMatcher;
	class FChainReach;
}

namespace PCGExData
{
	class FFacade;
	class FPointIOCollection;
}

USTRUCT(BlueprintType)
struct FPCGExDataMatchFilterConfig
{
	GENERATED_BODY()

	/** How match rules are combined. All = every rule must pass. Any = at least one rule must pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExMapMatchMode Mode = EPCGExMapMatchMode::All;

	/** Invert the result of this filter. When inverted, collections that match will fail the filter instead of passing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bInvert = false;

	/** Recursive rules chain through the other data being filtered:
	 *  a data passes when a chain of matches through them reaches a target. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	bool bChainThroughFilteredData = false;

	/** Suppress the warnings logged when Chain Through Filtered Data can't chain. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable, EditCondition="bChainThroughFilteredData", EditConditionHides))
	bool bQuietChainWarnings = false;
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

	/** Rules kept to chain over data only a host knows; empty unless chaining is on and a rule is recursive. */
	UPROPERTY()
	TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>> ChainRules;

	virtual bool SupportsCollectionEvaluation() const override
	{
		return true;
	}

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override
	{
		return true;
	}

	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	FORCEINLINE bool WantsChains() const
	{
		return !ChainRules.IsEmpty();
	}

	/** Built once per filtered collection and shared by every filter instance; null, and warned about once, if it can't be. */
	TSharedPtr<const PCGExMatching::FChainReach> GetChainReach(FPCGExContext* InContext, const TSharedPtr<PCGExData::FPointIOCollection>& InFilteredData) const;

	/** Once per factory, unless quieted: per-point hosts don't hand filters the data they filter, so only direct matches count. */
	void WarnChainsUnavailable(FPCGExContext* InContext) const;

protected:
	mutable FRWLock ChainReachLock;
	mutable TArray<TPair<TWeakPtr<PCGExData::FPointIOCollection>, TSharedPtr<const PCGExMatching::FChainReach>>> ChainReaches;
	mutable std::atomic<bool> bWarnedChainsUnavailable{false};
};

namespace PCGExPointFilter
{
	class FDataMatchFilter final : public TFactoryFilter<UPCGExDataMatchFilterFactory>
	{
	public:
		using TFactoryFilter::TFactoryFilter;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

	protected:
		// Uninverted. InFilteredData may be null; chains need it.
		bool Matches(PCGExData::FPointIO& InIO, const TSharedPtr<PCGExData::FPointIOCollection>& InFilteredData) const;
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/filter-data-match"))
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

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif

protected:
	virtual bool IsCacheable() const override
	{
		return false;
	}
};
