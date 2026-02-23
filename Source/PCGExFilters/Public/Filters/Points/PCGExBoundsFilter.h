// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"
#include "PCGExFilterCommon.h"
#include "Data/PCGExTaggedData.h"
#include "Math/PCGExMathBounds.h"
#include "PCGExMatching/Public/Core/PCGExMatchRuleFactoryProvider.h"

#include "PCGExBoundsFilter.generated.h"

namespace PCGExMath::OBB
{
	class FCollection;
}

namespace PCGExMatching
{
	class FDataMatcher;
}

UENUM()
enum class EPCGExBoundsCheckType : uint8
{
	Intersects           = 0 UMETA(DisplayName = "Intersects", Tooltip="Point's OBB overlaps target OBBs", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Intersects"),
	IsInside             = 1 UMETA(DisplayName = "Is Inside", Tooltip="Point center is inside target OBBs", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside"),
	IsInsideOrOn         = 2 UMETA(DisplayName = "Is Inside or On", Tooltip="Point center is inside or on boundary of target OBBs", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside or On"),
	IsInsideOrIntersects = 3 UMETA(DisplayName = "Is Inside or Intersects", Tooltip="Point center inside OR point's OBB overlaps target OBBs.", ActionIcon="PCGEx.Pin.OUT_Filter", SearchHints = "Inside Intersects"),
};

UENUM()
enum class EPCGExBoundsFilterCompareMode : uint8
{
	PerPointBounds   = 0 UMETA(DisplayName = "Per Point Bounds", Tooltip="Test each point individually"),
	CollectionBounds = 1 UMETA(DisplayName = "Collection Bounds", Tooltip="Test using collection's combined bounds"),
};

USTRUCT(BlueprintType)
struct FPCGExBoundsFilterConfig
{
	GENERATED_BODY()

	FPCGExBoundsFilterConfig()
	{
	}

	/** How to compare bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBoundsFilterCompareMode Mode = EPCGExBoundsFilterCompareMode::PerPointBounds;

	/** Bounds to use on target bounds data. (Those are the bounds connected to the filter) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExPointBoundsSource BoundsTarget = EPCGExPointBoundsSource::ScaledBounds;

	/** Type of bounds check to perform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExBoundsCheckType CheckType = EPCGExBoundsCheckType::Intersects;

	/** Bounds to use on input points (the points being filtered). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="CheckType == EPCGExBoundsCheckType::Intersects || CheckType == EPCGExBoundsCheckType::IsInsideOrIntersects", EditConditionHides))
	EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;

	/** Shape type for testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExBoxCheckMode TestMode = EPCGExBoxCheckMode::Box;

	/** Epsilon value used to slightly expand target bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="TestMode == EPCGExBoxCheckMode::ExpandedBox || TestMode == EPCGExBoxCheckMode::ExpandedSphere", EditConditionHides))
	double Expansion = 10;

	/** If enabled, invert the result of the test. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** If enabled, a collection will never be tested against itself. */
	bool bIgnoreSelf = false;

	/** Data matching settings. When enabled, only bounds data that matches the input being tested will be considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFilterMatchingDetails DataMatching;

	/** If enabled, uses collection bounds as a single proxy point instead of per-point testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bCheckAgainstDataBounds = false;
};

/**
 * Factory for bounds-based point filters.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExBoundsFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExBoundsFilterConfig Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExMatchRuleFactoryData>> MatchRuleFactories;

	TArray<TSharedPtr<PCGExData::FFacade>> BoundsDataFacades;
	TArray<TSharedPtr<PCGExMath::OBB::FCollection>> Collections;

	virtual bool Init(FPCGExContext* InContext) override;

	virtual bool SupportsCollectionEvaluation() const override { return Config.bCheckAgainstDataBounds; }
	virtual bool SupportsProxyEvaluation() const override { return true; }

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	virtual bool WantsPreparation(FPCGExContext* InContext) override { return true; }
	virtual PCGExFactories::EPreparationResult Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override;

	virtual void BeginDestroy() override;
};

namespace PCGExPointFilter
{
	class FBoundsFilter final : public ISimpleFilter
	{
	public:
		explicit FBoundsFilter(const TObjectPtr<const UPCGExBoundsFilterFactory>& InFactory)
			: ISimpleFilter(InFactory), TypedFilterFactory(InFactory)
		{
		}

		const TObjectPtr<const UPCGExBoundsFilterFactory> TypedFilterFactory;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const PCGExData::FProxyPoint& Point) const override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FBoundsFilter() override = default;

	private:
		// Cached config
		const TArray<TSharedPtr<PCGExMath::OBB::FCollection>>* Collections = nullptr;
		TArray<TSharedPtr<PCGExMath::OBB::FCollection>> FilteredCollections;
		EPCGExPointBoundsSource BoundsSource = EPCGExPointBoundsSource::ScaledBounds;
		EPCGExBoundsCheckType CheckType = EPCGExBoundsCheckType::Intersects;
		EPCGExBoxCheckMode CheckMode = EPCGExBoxCheckMode::Box;
		float Expansion = 0.0f;
		bool bInvert = false;
		bool bIgnoreSelf = false;
		bool bCheckAgainstDataBounds = false;
		bool bCollectionTestResult = false;
		bool bUseCollectionBounds = false;

		// Per-point matching: builds a per-point filtered collections list instead of an exclude set.
		// See FDistanceFilter for the general matching pattern explanation.
		TSharedPtr<PCGExMatching::FDataMatcher> InverseMatcher;
		TArray<FPCGExTaggedData> BoundsCandidates;
		bool bNoMatchResult = false;

		// Core test implementation
		bool TestPoint(const FVector& Position, const FTransform& Transform, const FBox& LocalBox) const;
		bool TestPoint(const FVector& Position, const FTransform& Transform, const FBox& LocalBox, const TArray<TSharedPtr<PCGExMath::OBB::FCollection>>& InCollections) const;
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-inclusion-bounds"))
class UPCGExBoundsFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(BoundsFilterFactory, "Filter : Inclusion (Bounds)", "Creates a filter definition that compares dot value of two vectors.", PCGEX_FACTORY_NAME_PRIORITY)
	virtual TArray<FPCGPreConfiguredSettingsInfo> GetPreconfiguredInfo() const override;
#endif

	virtual void ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo) override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Filter Config. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExBoundsFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual bool ShowMissingDataPolicy_Internal() const override { return true; }
#endif
};
