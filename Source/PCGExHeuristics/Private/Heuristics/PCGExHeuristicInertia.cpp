// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Heuristics/PCGExHeuristicInertia.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"
#include "Math/PCGExMath.h"


double FPCGExHeuristicInertia::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	return GetScoreInternal(GlobalInertiaScore);
}

double FPCGExHeuristicInertia::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	if (TravelStack)
	{
		int32 PathNodeIndex = PCGEx::NH64A(TravelStack->Get(From.Index));
		int32 PathEdgeIndex = -1;

		if (PathNodeIndex != -1)
		{
			FVector Avg = Cluster->GetDir(PathNodeIndex, From.Index);
			int32 Sampled = 1;
			while (PathNodeIndex != -1 && Sampled < MaxSamples)
			{
				const int32 CurrentIndex = PathNodeIndex;
				PCGEx::NH64(TravelStack->Get(CurrentIndex), PathNodeIndex, PathEdgeIndex);
				if (PathNodeIndex != -1)
				{
					Avg += Cluster->GetDir(PathNodeIndex, CurrentIndex);
					Sampled++;
				}
			}

			if (!bIgnoreIfNotEnoughSamples || Sampled == MaxSamples)
			{
				const double Dot = FVector::DotProduct((Avg / Sampled).GetSafeNormal(), Cluster->GetDir(From.Index, To.Index));

				return GetScoreInternal(PCGExMath::Remap(Dot, -1, 1, 1, 0));
			}
		}
	}

	return GetScoreInternal(FallbackInertiaScore);
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryInertia::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicInertia)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	NewOperation->GlobalInertiaScore = Config.GlobalInertiaScore;
	NewOperation->FallbackInertiaScore = Config.FallbackInertiaScore;
	NewOperation->MaxSamples = Config.Samples;
	NewOperation->bIgnoreIfNotEnoughSamples = Config.bIgnoreIfNotEnoughSamples;
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Inertia, {})

UPCGExFactoryData* UPCGExHeuristicsInertiaProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryInertia* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryInertia>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsInertiaProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0)),
		Config.bInvert);
}
#endif
