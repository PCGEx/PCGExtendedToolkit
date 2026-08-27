// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExFilterFactoryProvider.h"
#include "Filters/PCGExAdjacency.h"

#include "PCGExNodeEdgeAngleFilter.generated.h"


USTRUCT(BlueprintType)
struct FPCGExNodeEdgeAngleFilterConfig
{
	GENERATED_BODY()

	FPCGExNodeEdgeAngleFilterConfig()
	{
	}

	/** What should this filter return when dealing with leaves nodes? (node that only have one edge) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFilterFallback LeavesFallback = EPCGExFilterFallback::Fail;

	/** What should this filter return when dealing with complex, non-binary nodes? (node that have more that two edges) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFilterFallback NonBinaryFallback = EPCGExFilterFallback::Fail;

	/** Dot comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExDotComparisonDetails DotComparisonDetails;

	/** Whether the result of the filter should be inverted or not. This also inverts fallback results. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	bool bInvert = false;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExNodeEdgeAngleFilterFactory : public UPCGExNodeFilterFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNodeEdgeAngleFilterConfig Config;

	virtual bool RegisterConsumableAttributesWithData(FPCGExContext* InContext, const UPCGData* InData) const override;

	virtual TSharedPtr<PCGExPointFilter::IFilter> CreateFilter() const override;
};

class FNodeEdgeAngleFilter final : public PCGExClusterFilter::IVtxFilter
{
public:
	explicit FNodeEdgeAngleFilter(const UPCGExNodeEdgeAngleFilterFactory* InFactory)
		: IVtxFilter(InFactory)
		  , TypedFilterFactory(InFactory)
	{
		DotComparison = InFactory->Config.DotComparisonDetails;
	}

	const UPCGExNodeEdgeAngleFilterFactory* TypedFilterFactory;

	bool bLeavesFallback = true;
	bool bNonBinaryFallback = true;

	FPCGExDotComparisonDetails DotComparison;

	TSharedPtr<PCGExData::TBuffer<FVector>> OperandDirection;

	virtual bool Init(FPCGExContext* InContext, const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedRef<PCGExData::FFacade>& InPointDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual bool Test(const PCGExClusters::FNode& Node) const override;

	virtual ~FNodeEdgeAngleFilter() override;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="filters/vtx-filters/vtx-filter-edge-angle"))
class UPCGExNodeEdgeAngleFilterProviderSettings : public UPCGExVtxFilterProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(NodeEdgeAngleFilterFactory, "Vtx Filter : Edge Angle", "Dot product comparison of connected edges against themselves. Mostly useful on binary nodes only.", PCGEX_FACTORY_NAME_PRIORITY)

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(FilterCluster);
	}
#endif

	/** Test Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNodeEdgeAngleFilterConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
