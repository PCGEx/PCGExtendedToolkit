// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPathfinding.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExPathQuery.h"
#include "Core/PCGExPlotQuery.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "GoalPickers/PCGExGoalPicker.h"

namespace PCGExPathfinding
{
	bool FNodePick::ResolveNode(const TSharedRef<PCGExClusters::FCluster>& InCluster, const FPCGExNodeSelectionDetails& SelectionDetails)
	{
		if (Node != nullptr)
		{
			return true;
		}

		const FVector SourcePosition = Point.GetLocation();
		const int32 NodeIndex = SelectionDetails.PickClosestNode(*InCluster, SourcePosition);
		if (NodeIndex == -1)
		{
			return false;
		}
		Node = InCluster->GetNode(NodeIndex);
		return true;
	}

	void ProcessGoals(const TSharedPtr<PCGExData::FFacade>& InSeedDataFacade, const UPCGExGoalPicker* GoalPicker, TFunction<void(int32, int32)>&& GoalFunc)
	{
		for (int PointIndex = 0; PointIndex < InSeedDataFacade->Source->GetNum(); PointIndex++)
		{
			const PCGExData::FConstPoint& Seed = InSeedDataFacade->GetInPoint(PointIndex);

			if (GoalPicker->OutputMultipleGoals())
			{
				TArray<int32> GoalIndices;
				GoalPicker->GetGoalIndices(Seed, GoalIndices);
				for (const int32 GoalIndex : GoalIndices)
				{
					if (GoalIndex < 0)
					{
						continue;
					}
					GoalFunc(PointIndex, GoalIndex);
				}
			}
			else
			{
				const int32 GoalIndex = GoalPicker->GetGoalIndex(Seed);
				if (GoalIndex < 0)
				{
					continue;
				}
				GoalFunc(PointIndex, GoalIndex);
			}
		}
	}

	void MarkQueryVisited(const PCGExClusters::FCluster& Cluster, const FPathQuery& Query, int32* VtxVisitedCounts, int32* EdgeVisitedCounts)
	{
		// A* paths are simple (no repeated node within a single query), so a direct pass needs no dedup.
		if (VtxVisitedCounts)
		{
			for (const int32 NodeIndex : Query.PathNodes)
			{
				FPlatformAtomics::InterlockedIncrement(&VtxVisitedCounts[Cluster.GetNodePointIndex(NodeIndex)]);
			}
		}

		if (EdgeVisitedCounts)
		{
			for (const int32 EdgeIndex : Query.PathEdges)
			{
				FPlatformAtomics::InterlockedIncrement(&EdgeVisitedCounts[Cluster.GetEdge(EdgeIndex)->PointIndex]);
			}
		}
	}

	void MarkPlotVisited(const PCGExClusters::FCluster& Cluster, const FPlotQuery& Plot, int32* VtxVisitedCounts, int32* EdgeVisitedCounts)
	{
		// One plot is one output path. Gather the distinct elements its sub-paths visit, then
		// increment each once so a shared junction counts +1 rather than once per sub-path.
		if (VtxVisitedCounts)
		{
			TSet<int32> DistinctVtx;
			for (const TSharedPtr<FPathQuery>& SubQuery : Plot.SubQueries)
			{
				if (!SubQuery || !SubQuery->IsQuerySuccessful())
				{
					continue;
				}
				for (const int32 NodeIndex : SubQuery->PathNodes)
				{
					DistinctVtx.Add(Cluster.GetNodePointIndex(NodeIndex));
				}
			}
			for (const int32 PointIndex : DistinctVtx)
			{
				FPlatformAtomics::InterlockedIncrement(&VtxVisitedCounts[PointIndex]);
			}
		}

		if (EdgeVisitedCounts)
		{
			TSet<int32> DistinctEdges;
			for (const TSharedPtr<FPathQuery>& SubQuery : Plot.SubQueries)
			{
				if (!SubQuery || !SubQuery->IsQuerySuccessful())
				{
					continue;
				}
				for (const int32 EdgeIndex : SubQuery->PathEdges)
				{
					DistinctEdges.Add(Cluster.GetEdge(EdgeIndex)->PointIndex);
				}
			}
			for (const int32 PointIndex : DistinctEdges)
			{
				FPlatformAtomics::InterlockedIncrement(&EdgeVisitedCounts[PointIndex]);
			}
		}
	}
}
