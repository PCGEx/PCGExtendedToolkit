// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Heuristics/PCGExHeuristicSteepness.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"
#include "Math/PCGExMath.h"


void FPCGExHeuristicSteepness::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	UpwardVector = UpwardVector.GetSafeNormal();
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);
}

double FPCGExHeuristicSteepness::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	return GetScoreInternal(GetDot(Cluster->GetPos(From), Cluster->GetPos(Goal)));
}

double FPCGExHeuristicSteepness::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	if (bAccumulate && TravelStack)
	{
		int32 PathNodeIndex = PCGEx::NH64A(TravelStack->Get(From.Index));
		int32 PathEdgeIndex = -1;

		if (PathNodeIndex != -1)
		{
			double Avg = GetDot(Cluster->GetPos(From), Cluster->GetPos(To));
			int32 Sampled = 1;
			while (PathNodeIndex != -1 && Sampled < MaxSamples)
			{
				const int32 CurrentIndex = PathNodeIndex;
				PCGEx::NH64(TravelStack->Get(CurrentIndex), PathNodeIndex, PathEdgeIndex);
				if (PathNodeIndex != -1)
				{
					Avg += GetDot(Cluster->GetPos(PathNodeIndex), Cluster->GetPos(CurrentIndex));
					Sampled++;
				}
			}

			return GetScoreInternal(Avg);
		}
	}

	return GetScoreInternal(GetDot(Cluster->GetPos(From), Cluster->GetPos(To)));
}

double FPCGExHeuristicSteepness::GetDot(const FVector& From, const FVector& To) const
{
	const double Dot = FVector::DotProduct((To - From).GetSafeNormal(), UpwardVector);
	return bAbsoluteSteepness ? FMath::Abs(Dot) : PCGExMath::Remap(Dot, -1, 1);
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactorySteepness::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicSteepness)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	NewOperation->bAccumulate = Config.bAccumulateScore;
	NewOperation->MaxSamples = Config.AccumulationSamples;
	NewOperation->UpwardVector = Config.UpVector;
	NewOperation->bAbsoluteSteepness = Config.bAbsoluteSteepness;
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Steepness, {})

UPCGExFactoryData* UPCGExHeuristicsSteepnessProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactorySteepness* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactorySteepness>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsSteepnessProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0)),
		Config.bInvert);
}
#endif
