// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "HAL/CriticalSection.h"
#include "PCGExVtxPropertyFactoryProvider.h"
#include "Factories/PCGExFactoryProvider.h"
#include "Sorting/PCGExSortingCommon.h"
#include "Sorting/PCGExSortingDetails.h"
#include "UObject/Object.h"

#include "PCGExVtxPropertySortedNeighbor.generated.h"

///

class UPCGExVtxPropertySortedNeighborFactory;

namespace PCGExData
{
	class FFacade;
}

namespace PCGExSorting
{
	class FSorter;
}

USTRUCT(BlueprintType)
struct FPCGExSortedNeighborConfig
{
	GENERATED_BODY()

	FPCGExSortedNeighborConfig()
	{
		// FAdjacencyData::Direction is stored as (vtx - neighbor), i.e. pointing away from the
		// neighbor. This node is about the direction *to* the selected neighbor, so invert by default.
		SortedNeighbor.bInvertDirection = true;
	}

	/** Whether the sorting rules read their values from the neighbor vtx (Point) or from the edge
	 *  connecting the evaluated vtx to that neighbor (Edge). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExClusterElement SortSource = EPCGExClusterElement::Vtx;

	/** Which extreme to point toward, as ranked by the plugged sorting rules.
	 *  Descending points toward the highest-ranked neighbor, Ascending toward the lowest-ranked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExSortDirection SortDirection = EPCGExSortDirection::Descending;

	/** Output for the selected neighbor (direction, length, indices, neighbor count). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Output"))
	FPCGExEdgeOutputWithIndexSettings SortedNeighbor = FPCGExEdgeOutputWithIndexSettings(TEXT("Sorted"));
};

/**
 *
 */
class FPCGExVtxPropertySortedNeighbor : public FPCGExVtxPropertyOperation
{
public:
	FPCGExSortedNeighborConfig Config;

	const UPCGExVtxPropertySortedNeighborFactory* Factory = nullptr;

	virtual bool PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade) override;
	virtual void ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP) override;

protected:
	bool bSortByEdge = false;

	// Sorter over the chosen source facade: shared across a batch's clusters for the Vtx source (one shared
	// vtx facade), or effectively per-cluster for the Edge source (each cluster has its own edge facade).
	TSharedPtr<PCGExSorting::FSorter> Sorter;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExVtxPropertySortedNeighborFactory : public UPCGExVtxPropertyFactoryData
{
	GENERATED_BODY()

public:
	FPCGExSortedNeighborConfig Config;

	UPROPERTY()
	TArray<FPCGExSortRuleConfig> SortingRules;

	virtual TSharedPtr<FPCGExVtxPropertyOperation> CreateOperation(FPCGExContext* InContext) const override;

	/** Builds (or reuses) a sorter for the given source facade. Reuse is held via weak references, so a
	 *  completed batch's facade is released rather than pinned alive for the whole execution. All clusters
	 *  of a batch share one vtx facade (and thus one sorter); each cluster has its own edge facade.
	 *  Thread-safe. Returns null when there are no usable sorting rules. */
	TSharedPtr<PCGExSorting::FSorter> GetOrBuildSorter(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSortFacade) const;

private:
	mutable FCriticalSection SorterLock;

	// Weak so a sorter (and the facade it strong-refs) lives only as long as some cluster's operation holds
	// it; otherwise this factory-level map would pin every batch's facade alive until end of execution.
	mutable TMap<PCGExData::FFacade*, TWeakPtr<PCGExSorting::FSorter>> SortersByFacade;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|VtxProperty", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-vtx-properties/vtx-sorted-neighbor"))
class UPCGExVtxPropertySortedNeighborSettings : public UPCGExVtxPropertyProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(VtxSortedNeighbor, "Vtx : Sorted Neighbor", "Sort connected neighbors using sorting rules and output the direction to the extreme one.", FName(GetDisplayName()))
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Sorted Neighbor Settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExSortedNeighborConfig Config;
};
