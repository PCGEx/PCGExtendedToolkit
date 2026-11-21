// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPathfinding.h"

class FPCGExSearchOperation;
struct FPCGExNodeSelectionDetails;

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExHeuristics
{
	class FHandler;
	class FLocalFeedbackHandler;
}

namespace PCGExPathfinding
{
	class FSearchAllocations;
	class FPathQuery;

	class PCGEXELEMENTSPATHFINDING_API FPathQuery : public TSharedFromThis<FPathQuery>
	{
	public:
		FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const FNodePick& InSeed, const FNodePick& InGoal, const int32 InQueryIndex);
		FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const PCGExData::FConstPoint& InSeed, const PCGExData::FConstPoint& InGoal, const int32 InQueryIndex);
		FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedPtr<FPathQuery>& PreviousQuery, const PCGExData::FConstPoint& InGoalPointRef, const int32 InQueryIndex);
		FPathQuery(const TSharedRef<PCGExClusters::FCluster>& InCluster, const TSharedPtr<FPathQuery>& PreviousQuery, const TSharedPtr<FPathQuery>& NextQuery, const int32 InQueryIndex);

		TSharedRef<PCGExClusters::FCluster> Cluster;

		FNodePick Seed;
		FNodePick Goal;

		EQueryPickResolution PickResolution = EQueryPickResolution::None;

		TArray<int32> PathNodes;
		TArray<int32> PathEdges;
		EPathfindingResolution Resolution = EPathfindingResolution::None;

		const int32 QueryIndex = -1;

		bool HasValidEndpoints() const
		{
			return Seed.IsValid() && Goal.IsValid() && PickResolution == EQueryPickResolution::Success;
		};

		bool HasValidPathPoints() const
		{
			return PathNodes.Num() >= 2;
		};

		bool IsQuerySuccessful() const
		{
			return Resolution == EPathfindingResolution::Success;
		};

		EQueryPickResolution ResolvePicks(const FPCGExNodeSelectionDetails& SeedSelectionDetails, const FPCGExNodeSelectionDetails& GoalSelectionDetails);

		/** Sets PickResolution from the picks as they stand, for picks resolved against this cluster beforehand. */
		EQueryPickResolution UpdatePickResolution();

		void AddPathNode(const int32 InNodeIndex, const int32 InEdgeIndex = -1);
		void SetResolution(const EPathfindingResolution InResolution);

		void FindPath(
			const TSharedPtr<FPCGExSearchOperation>& SearchOperation,
			const TSharedPtr<FSearchAllocations>& Allocations,
			const TSharedPtr<PCGExHeuristics::FHandler>& HeuristicsHandler,
			const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback);

		void AppendNodePoints(TArray<int32>& OutPoints, const int32 TruncateStart = 0, const int32 TruncateEnd = 0) const;

		void AppendEdgePoints(TArray<int32>& OutPoints) const;

		void Cleanup();
	};
}
