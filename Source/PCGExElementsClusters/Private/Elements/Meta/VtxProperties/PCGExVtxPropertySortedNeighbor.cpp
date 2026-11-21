// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Meta/VtxProperties/PCGExVtxPropertySortedNeighbor.h"

#include "PCGPin.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Misc/ScopeLock.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"


#define LOCTEXT_NAMESPACE "PCGExVtxPropertySortedNeighbor"
#define PCGEX_NAMESPACE PCGExVtxPropertySortedNeighbor

#pragma region FPCGExVtxPropertySortedNeighbor

bool FPCGExVtxPropertySortedNeighbor::PrepareForCluster(FPCGExContext* InContext, TSharedPtr<PCGExClusters::FCluster> InCluster, const TSharedPtr<PCGExData::FFacade>& InVtxDataFacade, const TSharedPtr<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!FPCGExVtxPropertyOperation::PrepareForCluster(InContext, InCluster, InVtxDataFacade, InEdgeDataFacade))
	{
		return false;
	}

	if (!Config.SortedNeighbor.Validate(InContext))
	{
		bIsValidOperation = false;
		return false;
	}

	bSortByEdge = Config.SortSource == EPCGExClusterElement::Edge;

	// Vtx sorts over the shared vtx facade (one sorter reused across the batch's clusters); Edge sorts over
	// this cluster's own edge facade. Either way the sorter compares point values directly.
	check(Factory);
	const TSharedRef<PCGExData::FFacade> SortFacade = bSortByEdge ? InEdgeDataFacade.ToSharedRef() : InVtxDataFacade.ToSharedRef();
	Sorter = Factory->GetOrBuildSorter(InContext, SortFacade);
	if (!Sorter)
	{
		bIsValidOperation = false;
		return false;
	}

	// Output is always written to the evaluated vtx, regardless of what we sorted by.
	Config.SortedNeighbor.Init(InVtxDataFacade.ToSharedRef());

	return bIsValidOperation;
}

void FPCGExVtxPropertySortedNeighbor::ProcessNode(PCGExClusters::FNode& Node, const TArray<PCGExClusters::FAdjacencyData>& Adjacency, const PCGExMath::FBestFitPlane& BFP)
{
	if (Adjacency.IsEmpty())
	{
		Config.SortedNeighbor.Set(Node.PointIndex, 0, FVector::ZeroVector, -1, -1, 0);
		return;
	}

	// Keep the single most extreme neighbor; Sort(A, B) is true when A ranks before B under the chosen
	// direction. The sort key is the neighbor vtx point, or the connecting edge point when sorting over edges.
	int32 IBest = 0;
	int32 BestSortIndex = bSortByEdge ? Cluster->GetEdge(Adjacency[0].EdgeIndex)->PointIndex : Adjacency[0].NodePointIndex;

	for (int i = 1; i < Adjacency.Num(); i++)
	{
		const int32 CandSortIndex = bSortByEdge ? Cluster->GetEdge(Adjacency[i].EdgeIndex)->PointIndex : Adjacency[i].NodePointIndex;
		if (Sorter->Sort(CandSortIndex, BestSortIndex))
		{
			IBest = i;
			BestSortIndex = CandSortIndex;
		}
	}

	Config.SortedNeighbor.Set(Node.PointIndex, Adjacency[IBest], Cluster->GetNode(Adjacency[IBest].NodeIndex)->Num());
}

#pragma endregion

#pragma region UPCGExVtxPropertySortedNeighborFactory

TSharedPtr<FPCGExVtxPropertyOperation> UPCGExVtxPropertySortedNeighborFactory::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(VtxPropertySortedNeighbor)
	PCGEX_VTX_EXTRA_CREATE
	NewOperation->Factory = this;
	return NewOperation;
}

TSharedPtr<PCGExSorting::FSorter> UPCGExVtxPropertySortedNeighborFactory::GetOrBuildSorter(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InSortFacade) const
{
	PCGExData::FFacade* Key = &InSortFacade.Get();

	FScopeLock Lock(&SorterLock);

	// Reuse the sorter while any cluster of this facade still holds it (vtx clusters share one facade). The
	// entry is weak, so once the batch's operations are gone the sorter -- and its strong ref to the facade --
	// is released instead of being pinned for the whole execution; a dead/reused-address entry just rebuilds.
	if (const TWeakPtr<PCGExSorting::FSorter>* Existing = SortersByFacade.Find(Key))
	{
		if (TSharedPtr<PCGExSorting::FSorter> Pinned = Existing->Pin())
		{
			return Pinned;
		}
	}

	if (SortingRules.IsEmpty())
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Vtx : Sorted Neighbor -- no sorting rules provided."));
		return nullptr;
	}

	TSharedPtr<PCGExSorting::FSorter> NewSorter = MakeShared<PCGExSorting::FSorter>(InContext, InSortFacade, SortingRules);
	NewSorter->SortDirection = Config.SortDirection;
	if (!NewSorter->Init(InContext))
	{
		return nullptr;
	}

	SortersByFacade.Add(Key, NewSorter);
	return NewSorter;
}

#pragma endregion

#pragma region UPCGExVtxPropertySortedNeighborSettings

TArray<FPCGPinProperties> UPCGExVtxPropertySortedNeighborSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Required);
	return PinProperties;
}

UPCGExFactoryData* UPCGExVtxPropertySortedNeighborSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExVtxPropertySortedNeighborFactory* NewFactory = InContext->ManagedObjects->New<UPCGExVtxPropertySortedNeighborFactory>();
	NewFactory->Config = Config;
	NewFactory->SortingRules = PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules);
	return Super::CreateFactory(InContext, NewFactory);
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
