// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Heuristics/PCGExHeuristicDistance.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"


void FPCGExHeuristicDistance::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);
	BoundsSize = InCluster->Bounds.GetSize().Length();
	InvBoundsSize = BoundsSize > UE_SMALL_NUMBER ? 1.0 / BoundsSize : 1.0;
}

double FPCGExHeuristicDistance::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	return GetScoreInternal(Cluster->GetDist(From, Goal) * InvBoundsSize);
}

double FPCGExHeuristicDistance::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	// EdgeLengths are raw world-space; normalize by the cluster bounds so g shares h's scale
	// (GetGlobalScore). Any other scale breaks A*: a larger g drowns h (degrades toward Dijkstra),
	// a smaller g lets h dominate (degrades toward greedy best-first).
	return GetScoreInternal((*Cluster->EdgeLengths)[Edge.Index] * InvBoundsSize);
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryShortestDistance::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicDistance)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(ShortestDistance, {})

UPCGExFactoryData* UPCGExHeuristicsShortestDistanceProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryShortestDistance* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryShortestDistance>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsShortestDistanceProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0));
}
#endif
