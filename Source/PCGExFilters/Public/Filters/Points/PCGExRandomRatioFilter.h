// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/RichCurve.h"
#include "HAL/CriticalSection.h"
#include "UObject/Object.h"

#include "Core/PCGExFilterFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExDetailsNoise.h"
#include "Details/PCGExInputShorthandsDetails.h"

#include "PCGExRandomRatioFilter.generated.h"

USTRUCT(BlueprintType)
struct FPCGExRandomRatioFilterConfig
{
	GENERATED_BODY()

	/** Random selection settings (seed, ratio, mode). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExRandomRatioDetails Random;

	/** Weight each element's chance of being picked. Amount and clamps still decide how many pass. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWeighted = false;

	/** Pick weight, per point or per dataset (@Data). Zero-weight elements are drawn last, only if the amount requires it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bWeighted"))
	FPCGExInputShorthandSelectorDoubleAbs Weight = FPCGExInputShorthandSelectorDoubleAbs(FName("Weight"), 1, true);

	/** Exponent applied to the weights before the draw. Above 1 sharpens the contrast between weights, below 1 flattens it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName=" └─ Contrast", EditCondition="bWeighted", EditConditionHides, ClampMin=0, UIMin=0))
	double WeightContrast = 1;

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
	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExPointFilter
{
	class FRandomRatioFilter final : public ISimpleFilter
	{
	protected:
		TBitArray<> PointPicks;

		// Data-level draw: one for the whole pin, built on first use and immutable once published.
		mutable FCriticalSection CollectionPicksLock;
		mutable std::atomic<bool> bCollectionPicksBuilt{false};
		mutable bool bCollectionPicksValid = false;
		mutable TSet<int32> CollectionPicks;
		mutable const PCGExData::FPointIOCollection* BuiltForCollection = nullptr;

		void BuildCollectionPicks(const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const;

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

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(RandomCompareFilterFactory, "Filter : Random (Ratio)", "Passes an exact ratio or count of elements, selected at random.", PCGEX_FACTORY_NAME_PRIORITY)
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
