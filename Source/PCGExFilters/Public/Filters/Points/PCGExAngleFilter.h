// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExPointFilter.h"

#include "PCGExAngleFilter.generated.h"

UENUM()
enum class EPCGExAngleFilterMode : uint8
{
	Curvature = 0 UMETA(DisplayName = "Curvature", Tooltip="Check against the dot product of (Prev to Current) -> (Current to Next)"),
	Spread    = 1 UMETA(DisplayName = "Spread", Tooltip="Check against the dot product of (Current to Prev) -> (Current to Next)"),
};

USTRUCT(BlueprintType)
struct FPCGExAngleFilterConfig
{
	GENERATED_BODY()

	FPCGExAngleFilterConfig()
	{
	}

	/** Filter mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExAngleFilterMode Mode = EPCGExAngleFilterMode::Curvature;

	/** What should this filter return when dealing with first points? (if the data doesn't have @Data.IsClosed = true, otherwise wraps) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFilterFallback FirstPointFallback = EPCGExFilterFallback::Fail;

	/** What should this filter return when dealing with last points? (if the data doesn't have @Data.IsClosed = true, otherwise wraps) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFilterFallback LastPointFallback = EPCGExFilterFallback::Fail;

	/** Dot comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExDotComparisonDetails DotComparisonDetails;

	/** Whether the result of the filter should be inverted or not. This also inverts fallback results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bInvert = false;

	void Sanitize()
	{
	}
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExAngleFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExAngleFilterConfig Config;

	virtual bool Init(FPCGExContext* InContext) override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;

	// Needs prev/next path neighbors; collection-level evaluation is meaningless here.
	virtual bool SupportsCollectionEvaluation() const override
	{
		return false;
	}

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;
};

namespace PCGExPointFilter
{
	class FAngleFilter final : public ISimpleFilter
	{
	public:
		explicit FAngleFilter(const TObjectPtr<const UPCGExAngleFilterFactory>& InFactory)
			: ISimpleFilter(InFactory)
			  , TypedFilterFactory(InFactory)
		{
			DotComparison = TypedFilterFactory->Config.DotComparisonDetails;
		}

		const TObjectPtr<const UPCGExAngleFilterFactory> TypedFilterFactory;

		bool bClosedLoop = false;
		int32 LastIndex = -1;

		FPCGExDotComparisonDetails DotComparison;
		TConstPCGValueRange<FTransform> InTransforms;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;

		virtual ~FAngleFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/math/filter-angle"))
class UPCGExAngleFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(AngleFilterFactory, "Filter : Angle", "Creates a filter definition that compares dot value of the direction of a point toward its previous and next points.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExAngleFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
