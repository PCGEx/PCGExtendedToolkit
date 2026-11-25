// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Factories/PCGExOperation.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "UObject/Object.h"
#include "Utils/PCGExCurveLookup.h"

namespace PCGEx
{
	class FHashLookup;
}

namespace PCGExGraphs
{
	struct FEdge;
}

namespace PCGExClusters
{
	struct FNode;
	class FCluster;
}

/**
 *
 */
class PCGEXHEURISTICS_API FPCGExHeuristicOperation : public FPCGExOperation
{
public:
	bool bInvert = false;
	double ReferenceWeight = 1;
	double WeightFactor = 1;
	bool bUseLocalWeightMultiplier = false;
	FVector UVWSeed = FVector::OneVector * -1;
	FVector UVWGoal = FVector::OneVector;
	EPCGExClusterElement LocalWeightMultiplierSource = EPCGExClusterElement::Vtx;
	FPCGAttributePropertyInputSelector WeightMultiplierAttribute;

	PCGExFloatLUT ScoreCurve = nullptr;

	bool bHasCustomLocalWeightMultiplier = false;

	/** True when GetEdgeScore depends only on From/To/Edge -- no Seed/Goal/TravelStack, and no state
	 * that mutates between queries -- so the handler can bake it once per directed edge. */
	virtual bool HasStaticEdgeScore() const
	{
		return false;
	}

	/** True when GetEdgeScore depends on the route that led to From, read from TravelStack as predecessors. */
	virtual bool UsesTravelHistory() const
	{
		return false;
	}

	virtual void PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster);

	virtual double GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const;


	virtual double GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack = nullptr) const;


	double GetCustomWeightMultiplier(const int32 PointIndex, const int32 EdgeIndex) const;

	FORCEINLINE FVector GetSeedUVW() const
	{
		return UVWSeed;
	}

	FORCEINLINE FVector GetGoalUVW() const
	{
		return UVWGoal;
	}

protected:
	TSharedPtr<const PCGExClusters::FCluster> Cluster;
	TArray<double> LocalWeightMultiplier;

	double GetScoreInternal(const double InTime) const;
};
