// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Heuristics/PCGExHeuristicGradient.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Math/PCGExMath.h"

#define LOCTEXT_NAMESPACE "PCGExCreateHeuristicGradient"
#define PCGEX_NAMESPACE CreateHeuristicGradient

void FPCGExHeuristicGradient::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	FPCGExHeuristicOperation::PrepareForCluster(InCluster);

	const int32 NumNodes = InCluster->Nodes->Num();
	CachedValues.SetNumZeroed(NumNodes);

	InvMaxEdgeLength = InCluster->MaxEdgeLength > UE_SMALL_NUMBER ? 1.0 / InCluster->MaxEdgeLength : 1.0;

	// Read attribute values from vertices
	const TSharedPtr<PCGExData::TBuffer<double>> Values = PrimaryDataFacade->GetBroadcaster<double>(Attribute, false, true);

	if (!Values)
	{
		PCGEX_LOG_INVALID_SELECTOR_C(Context, Heuristic, Attribute)
		return;
	}

	// Cache values indexed by node index
	for (const PCGExClusters::FNode& Node : (*InCluster->Nodes))
	{
		CachedValues[Node.Index] = Values->Read(Node.PointIndex);
	}

	// Update gradient range if auto-captured
	if (Mode == EPCGExGradientMode::AvoidChange || Mode == EPCGExGradientMode::SeekChange)
	{
		GradientRange = MaxGradient - MinGradient;
		if (GradientRange <= 0)
		{
			GradientRange = 1;
		}
	}
}

double FPCGExHeuristicGradient::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	// Global score based on gradient toward goal
	if (CachedValues.IsEmpty())
	{
		return GetScoreInternal(0);
	}

	const double FromValue = CachedValues[From.Index];
	const double GoalValue = CachedValues[Goal.Index];
	const double Gradient = GoalValue - FromValue;

	switch (Mode)
	{
	case EPCGExGradientMode::FollowIncreasing:
		// If goal has higher value, that's good (low score). If lower, that's bad (high score).
		return GetScoreInternal(Gradient < 0 ? 1 : 0);

	case EPCGExGradientMode::FollowDecreasing:
		// If goal has lower value, that's good (low score). If higher, that's bad (high score).
		return GetScoreInternal(Gradient > 0 ? 1 : 0);

	case EPCGExGradientMode::AvoidChange:
	case EPCGExGradientMode::SeekChange:
		return GetScoreInternal(0.5); // Neutral for global score

	default:
		return GetScoreInternal(0);
	}
}

double FPCGExHeuristicGradient::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	if (CachedValues.IsEmpty())
	{
		return GetScoreInternal(0);
	}

	const double FromValue = CachedValues[From.Index];
	const double ToValue = CachedValues[To.Index];
	double Gradient = ToValue - FromValue;

	// Optionally normalize by edge length. EdgeLengths are raw world-space; dividing by the
	// max-relative length (len/maxLen, in (0,1]) keeps gradients in a range the clamps below
	// can still discriminate -- per-world-unit slopes would collapse toward 0.
	if (bNormalizeByDistance)
	{
		const double EdgeLength = (*Cluster->EdgeLengths)[Edge.Index] * InvMaxEdgeLength;
		if (EdgeLength > KINDA_SMALL_NUMBER)
		{
			Gradient /= EdgeLength;
		}
	}

	double Score = 0;

	switch (Mode)
	{
	case EPCGExGradientMode::FollowIncreasing:
		// Positive gradient (increasing) = good = low score
		// Negative gradient (decreasing) = bad = high score
		// Remap from [-1, 1] to [1, 0] (inverted so increasing = lower score)
		Score = PCGExMath::Remap(FMath::Clamp(Gradient, -1.0, 1.0), -1, 1, 1, 0);
		break;

	case EPCGExGradientMode::FollowDecreasing:
		// Negative gradient (decreasing) = good = low score
		// Positive gradient (increasing) = bad = high score
		// Remap from [-1, 1] to [0, 1] (so decreasing = lower score)
		Score = PCGExMath::Remap(FMath::Clamp(Gradient, -1.0, 1.0), -1, 1, 0, 1);
		break;

	case EPCGExGradientMode::AvoidChange:
		// Large absolute gradient = bad = high score
		// Small absolute gradient = good = low score
	{
		const double AbsGradient = FMath::Abs(Gradient);
		Score = FMath::Clamp((AbsGradient - MinGradient) / GradientRange, 0.0, 1.0);
	}
	break;

	case EPCGExGradientMode::SeekChange:
		// Large absolute gradient = good = low score
		// Small absolute gradient = bad = high score
	{
		const double AbsGradient = FMath::Abs(Gradient);
		Score = 1.0 - FMath::Clamp((AbsGradient - MinGradient) / GradientRange, 0.0, 1.0);
	}
	break;
	}

	return GetScoreInternal(Score);
}

TSharedPtr<FPCGExHeuristicOperation> UPCGExHeuristicsFactoryGradient::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(HeuristicGradient)
	PCGEX_FORWARD_HEURISTIC_CONFIG
	NewOperation->Mode = Config.Mode;
	NewOperation->Attribute = Config.Attribute;
	NewOperation->bNormalizeByDistance = Config.bNormalizeByDistance;
	NewOperation->MinGradient = Config.MinGradient;
	NewOperation->MaxGradient = Config.MaxGradient;
	NewOperation->GradientRange = Config.MaxGradient - Config.MinGradient;
	return NewOperation;
}

PCGEX_HEURISTIC_FACTORY_BOILERPLATE_IMPL(Gradient, {})

void UPCGExHeuristicsFactoryGradient::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);
	FacadePreloader.Register<double>(InContext, Config.Attribute);
}

#if WITH_EDITOR
void UPCGExHeuristicsGradientProviderSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

UPCGExFactoryData* UPCGExHeuristicsGradientProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExHeuristicsFactoryGradient* NewFactory = InContext->ManagedObjects->New<UPCGExHeuristicsFactoryGradient>();
	PCGEX_FORWARD_HEURISTIC_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}

#if WITH_EDITOR
FString UPCGExHeuristicsGradientProviderSettings::GetDisplayName() const
{
	return TEXT("HX : Gradient ") + PCGExMetaHelpers::GetSelectorDisplayName(Config.Attribute) + TEXT(" @ ") + FString::Printf(TEXT("%.3f"), (static_cast<int32>(1000 * Config.WeightFactor) / 1000.0));
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
