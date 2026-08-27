// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Heuristics/PCGExHeuristicAzimuth.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Math/PCGExMath.h"

void FPCGExHeuristicAzimuth::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);

	// Pre-compute edge directions for faster GetEdgeScore lookups
	const int32 NumEdges = InCluster->Edges->Num();
	CachedEdgeDirections.SetNumUninitialized(NumEdges);

	const TArray<PCGExGraphs::FEdge>& EdgesRef = *InCluster->Edges;
	for (int32 i = 0; i < NumEdges; i++)
	{
		const PCGExGraphs::FEdge& Edge = EdgesRef[i];
		// Store direction from Start to End (normalized)
		CachedEdgeDirections[i] = InCluster->GetDir(
			InCluster->NodeIndexLookup->Get(Edge.Start),
			InCluster->NodeIndexLookup->Get(Edge.End));
	}
}

double FPCGExHeuristicAzimuth::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	const FVector Dir = Cluster->GetDir(Seed, Goal);
	const double Dot = FVector::DotProduct(Dir, Cluster->GetDir(From, Goal)) * -1;
	return GetScoreInternal(PCGExMath::Remap(Dot, -1, 1, 0, 1));
}

double FPCGExHeuristicAzimuth::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	// Use cached edge direction, but check if we're traversing in reverse
	FVector EdgeDir = CachedEdgeDirections[Edge.Index];
	if (Edge.Start != From.PointIndex)
	{
		EdgeDir = -EdgeDir;
	} // Reverse if traversing opposite direction

	const double Dot = FVector::DotProduct(EdgeDir, Cluster->GetDir(From, Goal));
	return GetScoreInternal(PCGExMath::Remap(Dot, -1, 1, 1, 0));
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryAzimuth::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicAzimuth)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Azimuth, {})

UPCGExFactoryData* UPCGExHeuristicsAzimuthProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryAzimuth* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryAzimuth>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsAzimuthProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0)),
		Config.bInvert);
}
#endif
