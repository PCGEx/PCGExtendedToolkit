// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Heuristics/PCGExHeuristicFeedback.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"

void FPCGExHeuristicFeedback::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);

	const int32 NumNodes = InCluster->Nodes->Num();
	const int32 NumEdges = InCluster->Edges->Num();

	NodeFeedbackCounts.SetNumZeroed(NumNodes);
	EdgeFeedbackCounts.SetNumZeroed(NumEdges);
}

double FPCGExHeuristicFeedback::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	const uint32 N = NodeFeedbackCounts[From.Index];
	// Use logarithmic scaling to prevent unbounded growth from visit counts
	return N ? GetScoreInternal(NodeScale * FMath::Loge(static_cast<double>(N) + 1.0)) : 0.0;
}

double FPCGExHeuristicFeedback::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	const uint32 N = NodeFeedbackCounts[To.Index];
	const uint32 E = EdgeFeedbackCounts[Edge.Index];

	if (bBinary)
	{
		return (N || E) ? GetScoreInternal(1) : GetScoreInternal(0);
	}

	// Use logarithmic scaling to prevent unbounded growth from visit counts
	// This keeps feedback influence meaningful without dominating other heuristics
	const double NW = N ? GetScoreInternal(NodeScale * FMath::Loge(static_cast<double>(N) + 1.0)) : 0.0;
	const double EW = E ? GetScoreInternal(EdgeScale * FMath::Loge(static_cast<double>(E) + 1.0)) : 0.0;

	return (NW + EW);
}

void FPCGExHeuristicFeedback::FeedbackPointScore(const PCGExClusters::FNode& Node)
{
	NodeFeedbackCounts[Node.Index]++;

	if (bBleed)
	{
		for (const PCGExGraphs::FLink Lk : Node.Links)
		{
			EdgeFeedbackCounts[Lk.Edge]++;
		}
	}
}

void FPCGExHeuristicFeedback::FeedbackScore(const PCGExClusters::FNode& Node, const PCGExGraphs::FEdge& Edge)
{
	NodeFeedbackCounts[Node.Index]++;

	if (bBleed)
	{
		for (const PCGExGraphs::FLink Lk : Node.Links)
		{
			EdgeFeedbackCounts[Lk.Edge]++;
		}
	}
	else
	{
		EdgeFeedbackCounts[Edge.Index]++;
	}
}

void FPCGExHeuristicFeedback::ResetFeedback()
{
	if (!NodeFeedbackCounts.IsEmpty())
	{
		FMemory::Memzero(NodeFeedbackCounts.GetData(), NodeFeedbackCounts.Num() * sizeof(uint32));
	}
	if (!EdgeFeedbackCounts.IsEmpty())
	{
		FMemory::Memzero(EdgeFeedbackCounts.GetData(), EdgeFeedbackCounts.Num() * sizeof(uint32));
	}
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryFeedback::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicFeedback)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	NewOperation->NodeScale = Config.VisitedPointsWeightFactor;
	NewOperation->EdgeScale = Config.VisitedEdgesWeightFactor;
	NewOperation->bBleed = Config.bAffectAllConnectedEdges;
	NewOperation->bBinary = Config.bBinary;
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Feedback, {})

UPCGExFactoryData* UPCGExHeuristicFeedbackProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryFeedback* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryFeedback>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicFeedbackProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0)),
		Config.bInvert);
}
#endif
