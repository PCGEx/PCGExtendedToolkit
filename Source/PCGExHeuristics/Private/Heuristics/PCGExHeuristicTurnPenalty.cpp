// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Heuristics/PCGExHeuristicTurnPenalty.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"

double FPCGExHeuristicTurnPenalty::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	return GetScoreInternal(GlobalScore);
}

double FPCGExHeuristicTurnPenalty::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	if (!TravelStack)
	{
		return GetScoreInternal(FallbackScore);
	}

	// Get the previous node index from TravelStack
	const int32 PrevNodeIndex = PCGEx::NH64A(TravelStack->Get(From.Index));
	if (PrevNodeIndex == -1)
	{
		return GetScoreInternal(FallbackScore);
	} // No previous node (at seed)

	// Get directions
	const FVector IncomingDir = Cluster->GetDir(PrevNodeIndex, From.Index);
	const FVector OutgoingDir = Cluster->GetDir(From, To);

	// Calculate angle between directions
	const double DotProduct = FVector::DotProduct(IncomingDir, OutgoingDir);

	// Clamp dot product to valid range for acos (floating point errors can cause issues)
	const double ClampedDot = FMath::Clamp(DotProduct, -1.0, 1.0);

	// Angle is 0 when going straight (dot = 1), PI when U-turn (dot = -1)
	double Angle = FMath::Acos(ClampedDot);

	if (!bAbsoluteAngle)
	{
		// Use cross product to determine turn direction (positive = left, negative = right in XY plane)
		const FVector Cross = FVector::CrossProduct(IncomingDir, OutgoingDir);
		if (Cross.Z < 0)
		{
			Angle = -Angle;
		}
	}
	else
	{
		Angle = FMath::Abs(Angle);
	}

	// Remap angle to 0-1 range based on thresholds
	if (Angle <= MinAngleRad)
	{
		return GetScoreInternal(0);
	}
	if (Angle >= MaxAngleRad)
	{
		return GetScoreInternal(1);
	}

	const double NormalizedAngle = (Angle - MinAngleRad) / AngleRange;
	return GetScoreInternal(NormalizedAngle);
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryTurnPenalty::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicTurnPenalty)
	PCGEX_FORWARD_HEURISTIC_CONFIG

	NewOperation->MinAngleRad = FMath::DegreesToRadians(Config.MinAngleThreshold);
	NewOperation->MaxAngleRad = FMath::DegreesToRadians(Config.MaxAngleThreshold);
	NewOperation->AngleRange = NewOperation->MaxAngleRad - NewOperation->MinAngleRad;
	NewOperation->bAbsoluteAngle = Config.bAbsoluteAngle;
	NewOperation->GlobalScore = Config.GlobalScore;
	NewOperation->FallbackScore = Config.FallbackScore;

	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(TurnPenalty, {})

UPCGExFactoryData* UPCGExHeuristicsTurnPenaltyProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryTurnPenalty* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryTurnPenalty>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsTurnPenaltyProviderSettings::GetDisplayName() const
{
	return PCGExCommon::FlagInvertLabel(
		GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Heuristics"), TEXT("HX")) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0)),
		Config.bInvert);
}
#endif
