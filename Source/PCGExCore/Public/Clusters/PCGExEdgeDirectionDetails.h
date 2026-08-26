// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExEdge.h"
#include "Metadata/PCGAttributePropertySelector.h"

#include "PCGExEdgeDirectionDetails.generated.h"

struct FPCGExSortRuleConfig;
struct FPCGExContext;

namespace PCGExSorting
{
	class FSorter;
}

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExData
{
	class FFacade;
	class FFacadePreloader;

	template <typename T>
	class TBuffer;
}

USTRUCT(BlueprintType)
struct PCGEXCORE_API FPCGExEdgeDirectionSettings
{
	GENERATED_BODY()

	/** Method to pick the edge direction amongst various possibilities.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExEdgeDirectionMethod DirectionMethod = EPCGExEdgeDirectionMethod::EndpointsOrder;

	/** Attribute picker for the selected Direction Method.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="DirectionMethod == EPCGExEdgeDirectionMethod::EdgeDotAttribute", EditConditionHides))
	FPCGAttributePropertyInputSelector DirSourceAttribute;

	/** Which way the edge direction points relative to the method's endpoint ordering. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExEdgeDirectionChoice DirectionChoice = EPCGExEdgeDirectionChoice::SmallestToGreatest;

	bool bAscendingDesired = false;
	TSharedPtr<PCGExData::TBuffer<FVector>> EdgeDirReader;

	TSharedPtr<PCGExSorting::FSorter> Sorter;

	void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<FPCGExSortRuleConfig>* InSortingRules = nullptr) const;

	bool Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TArray<FPCGExSortRuleConfig>* InSortingRules = nullptr, const bool bQuiet = false);

	bool InitFromParent(FPCGExContext* InContext, const FPCGExEdgeDirectionSettings& ParentSettings, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade, const bool bQuiet = false);

	bool RequiresSortingRules() const
	{
		return DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort;
	}

	bool RequiresEndpointsMetadata() const
	{
		return DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort;
	}

	bool RequiresEdgeMetadata() const
	{
		return DirectionMethod == EPCGExEdgeDirectionMethod::EdgeDotAttribute;
	}

	bool SortEndpoints(const PCGExClusters::FCluster* InCluster, PCGExGraphs::FEdge& InEdge) const;
	bool SortExtrapolation(const PCGExClusters::FCluster* InCluster, const int32 InEdgeIndex, const int32 StartNodeIndex, const int32 EndNodeIndex) const;
};
