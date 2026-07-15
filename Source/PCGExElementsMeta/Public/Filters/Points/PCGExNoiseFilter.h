// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExFilterFactoryProvider.h"
#include "UObject/Object.h"

#include "Core/PCGExPointFilter.h"
#include "Details/PCGExCompareShorthandsDetails.h"

#include "PCGExNoiseFilter.generated.h"


namespace PCGExNoise3D
{
	class FNoiseGenerator;
}

USTRUCT(BlueprintType)
struct FPCGExNoiseFilterConfig
{
	GENERATED_BODY()

	FPCGExNoiseFilterConfig() = default;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExCompareSelectorDouble Comparison = FPCGExCompareSelectorDouble(TEXT("OperandB"), 0.5);
};


/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter")
class UPCGExNoiseFilterFactory : public UPCGExPointFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseFilterConfig Config;
	TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseGenerator;

	virtual bool Init(FPCGExContext* InContext) override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExPointFilter
{
	class FNoiseFilter final : public ISimpleFilter
	{
	public:
		explicit FNoiseFilter(const TObjectPtr<const UPCGExNoiseFilterFactory>& InDefinition)
			: ISimpleFilter(InDefinition)
			  , TypedFilterFactory(InDefinition)
		{
		}

		TConstPCGValueRange<FTransform> InTransforms;
		const TObjectPtr<const UPCGExNoiseFilterFactory> TypedFilterFactory;

		TSharedPtr<PCGExDetails::TSettingValue<double>> OperandB;
		TSharedPtr<PCGExNoise3D::FNoiseGenerator> NoiseGenerator;

		virtual bool Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade) override;

		virtual bool Test(const int32 PointIndex) const override;
		virtual bool Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const override;

		virtual ~FNoiseFilter() override
		{
		}
	};
}

///

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Filter", meta=(PCGExNodeLibraryDoc="filters/point-filters/spatial/filter-noise"))
class UPCGExNoiseFilterProviderSettings : public UPCGExFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NoiseFilterFactory, "Filter : Noise", "Compare a value against spatial noise.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_BLEND(Filter, Noise3D);
	}
#endif

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	//~End UPCGSettings

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
