// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExSearchAllocations.h"

#include "PCGExH.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Utils/PCGExScoredQueue.h"

namespace PCGExPathfinding
{
	void FSearchAllocations::Init(const PCGExClusters::FCluster* InCluster)
	{
		NumNodes = InCluster->Nodes->Num();

		Visited.Init(false, NumNodes);
		TravelStack = MakeShared<PCGEx::FHashLookupArray>(PCGEx::NH64(-1, -1), NumNodes);
		ScoredQueue = MakeShared<PCGEx::FScoredQueue>(NumNodes);
	}

	void FSearchAllocations::InitGScore(const double InInitValue)
	{
		GScoreInit = InInitValue;
		GScore.Init(InInitValue, NumNodes);
	}

	void FSearchAllocations::Reset()
	{
		ResetSearchState(ScoredQueue, Visited, GScore, GScoreInit, TravelStack);
	}

	void FSearchAllocations::ResetSearchState(const TSharedPtr<PCGEx::FScoredQueue>& InQueue, TBitArray<>& InVisited, TArray<double>& InGScore, const double InGScoreInit, const TSharedPtr<PCGEx::FHashLookup>& InTravelStack) const
	{
		const TArray<int32>& Touched = InQueue->GetTouched();
		const bool bHasGScore = !InGScore.IsEmpty();

		// Restore only what the previous search dirtied, unless it visited most of the
		// cluster -- a dense sweep is then cheaper than scattered writes.
		if (Touched.Num() < NumNodes / 4)
		{
			for (const int32 Index : Touched)
			{
				InVisited[Index] = false;
				InTravelStack->Unset(Index);
				if (bHasGScore)
				{
					InGScore[Index] = InGScoreInit;
				}
			}
		}
		else
		{
			InVisited.SetRange(0, NumNodes, false);
			InTravelStack->Reset();
			if (bHasGScore)
			{
				for (double& Value : InGScore)
				{
					Value = InGScoreInit;
				}
			}
		}

		// Last: Reset() consumes the touched list.
		InQueue->Reset();
	}
}
