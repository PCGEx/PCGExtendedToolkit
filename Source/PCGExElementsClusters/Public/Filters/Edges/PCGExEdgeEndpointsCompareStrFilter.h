// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Utils/PCGExCompare.h"

#include "PCGExEdgeEndpointsCompareStrFilter.generated.h"

USTRUCT(BlueprintType)
struct FPCGExEdgeEndpointsCompareStrFilterConfig
{
	GENERATED_BODY()

	FPCGExEdgeEndpointsCompareStrFilterConfig()
	{
	}

	/** Attribute to compare */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;

	/** Comparison check */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Comparison"))
	EPCGExStringComparison Comparison = EPCGExStringComparison::StrictlyEqual;

	/** Invert the filter result (pass becomes fail and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExEdgeEndpointsCompareStrFilterFactory : public UPCGExEdgeFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExEdgeEndpointsCompareStrFilterConfig Config;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

namespace PCGExEdgeEndpointsCompareStr
{
	class FFilter final : public PCGExPointFilter::TFactoryFilter<UPCGExEdgeEndpointsCompareStrFilterFactory, PCGExClusterFilter::IEdgeFilter>
	{
	public:
		using TFactoryFilter::TFactoryFilter;

		TSharedPtr<PCGExData::TBuffer<FString>> StringBuffer;

		virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;
		virtual bool Test(const PCGExGraphs::FEdge& Edge) const override;

		virtual ~FFilter() override;
	};
}


/** Outputs a single GraphParam to be consumed by other nodes */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="filters/edge-filters/edge-filter-endpoints-compare-string"))
class UPCGExEdgeEndpointsCompareStrFilterProviderSettings : public UPCGExEdgeFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(EdgeEndpointsCompareStrFilterFactory, "Edge Filter : Endpoints Compare (String)", "Compare the value of an attribute on each of the edge endpoint.", PCGEX_FACTORY_NAME_PRIORITY)
#endif
	//~End UPCGSettings

	/** Test Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExEdgeEndpointsCompareStrFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
