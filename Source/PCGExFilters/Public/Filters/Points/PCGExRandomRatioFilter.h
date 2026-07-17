// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "UObject/Object.h"

#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExDetailsNoise.h"

#include "PCGExRandomRatioFilter.generated.h"

USTRUCT(BlueprintType)
struct FPCGExRandomRatioFilterConfig
{
	GENERATED_BODY()

	FPCGExRandomRatioFilterConfig()
	{
	}

	/** Random selection settings (seed, ratio, mode). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExRandomRatioDetails Random;

	/** Invert the filter result. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvertResult = false;
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExRandomRatioFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExRandomRatioFilterConfig Config;

	virtual bool SupportsCollectionEvaluation() const override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExPointFilter
{
	class FRandomRatioFilter final : public ISimpleFilter
	{
	protected:
		FRWLock CollectionLock;
		TSet<int32> PointPicks;

		bool bColPicksBuilt = false;
		TSet<int32> CollectionPicks;

		const TSet<int32>& GetCollectionPicks(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection);

	public:
		explicit FRandomRatioFilter(const TObjectPtr<const UPCGExRandomRatioFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		const TObjectPtr<const UPCGExRandomRatioFilterFactory> TypedFilterFactory;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;
		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;


		virtual ~FRandomRatioFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/filter-random-ratio"))
class UPCGExRandomRatioFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(RandomCompareFilterFactory, "Filter : Random (Ratio)", "Filter using a random value.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExRandomRatioFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
