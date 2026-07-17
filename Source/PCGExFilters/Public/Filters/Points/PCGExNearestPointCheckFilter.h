// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Filters/Points/PCGExNearestFilter.h"

#include "PCGExNearestPointCheckFilter.generated.h"


UENUM()
enum class EPCGExNearestCheckMode : uint8
{
	Nearest = 0 UMETA(DisplayName = "Nearest", ToolTip="Test only the single nearest target in range."),
	Any     = 1 UMETA(DisplayName = "Any In Range", ToolTip="Succeeds as soon as any target in range passes the filters. Requires a positive Max Distance."),
};

USTRUCT(BlueprintType)
struct FPCGExNearestPointCheckFilterConfig : public FPCGExNearestFilterConfigBase
{
	GENERATED_BODY()

	FPCGExNearestPointCheckFilterConfig() = default;

	/** How in-range targets are evaluated. Nearest tests only the closest; Any In Range succeeds if any in-range target passes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExNearestCheckMode Mode = EPCGExNearestCheckMode::Nearest;

	/** Invert the filter result (the nearest point passing the filters becomes a fail, and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** Result to return when no nearest/in-range target is found. Not affected by Invert. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExFilterFallback NoTargetFallback = EPCGExFilterFallback::Fail;
};


/**
 * Finds the nearest target point (or any in range) and returns whether it passes a set of sub-filters.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExNearestPointCheckFilterFactory : public UPCGExNearestFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNearestPointCheckFilterConfig Config;

	/** Filters evaluated against the candidate target point(s). */
	UPROPERTY()
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> FilterFactories;

	// One filter manager per target dataset, shared across all source filter instances. Index-aligned with FConstPoint::IO.
	TSharedPtr<TArray<TSharedPtr<PCGExPointFilter::FManager>>> TargetFilterManagers;

	// "Any In Range" verdict cache: per-target int8 array (-1 unknown, 0 fail, 1 pass), filled lazily
	// during Test so overlapping source ranges don't re-test a target. Only allocated when Mode == Any.
	TSharedPtr<TArray<TArray<int8>>> TargetResultCache;

	virtual bool Init(FPCGExContext* InContext) override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

protected:
	virtual void RegisterTargetDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool BuildTargetCaches(FPCGExContext* InContext) override;
};

namespace PCGExPointFilter
{
	class FNearestPointCheckFilter final : public FNearestFilter
	{
	public:
		explicit FNearestPointCheckFilter(const TObjectPtr<const UPCGExNearestPointCheckFilterFactory>& InDefinition)
			: FNearestFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
			TargetFilterManagers = TypedFilterFactory->TargetFilterManagers;
			TargetResultCache = TypedFilterFactory->TargetResultCache;
		}

		const TObjectPtr<const UPCGExNearestPointCheckFilterFactory> TypedFilterFactory;

		// Shared from the factory; index-aligned with the nearest target's IO.
		TSharedPtr<TArray<TSharedPtr<PCGExPointFilter::FManager>>> TargetFilterManagers;

		// Shared from the factory (Any mode only); inner index == target point index, outer == target IO.
		TSharedPtr<TArray<TArray<int8>>> TargetResultCache;

		bool bAnyMode = false;
		bool bNoTargetResult = false;

		virtual bool Test(const int32 PointIndex) const override;

	protected:
		virtual bool InitNearest(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

	public:
		virtual ~FNearestPointCheckFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-check-nearest"))
class UPCGExNearestPointCheckFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NearestPointCheckFilterFactory, "Filter : Check Nearest", "Finds the nearest target point and tests it against a set of filters.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNearestPointCheckFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;

	virtual bool ShowMissingDataPolicy_Internal() const override
	{
		return true;
	}
#endif
};
