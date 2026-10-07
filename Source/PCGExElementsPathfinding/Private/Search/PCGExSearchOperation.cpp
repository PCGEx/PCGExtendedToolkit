// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Search/PCGExSearchOperation.h"
#include "Core/PCGExSearchAllocations.h"

void FPCGExSearchOperation::PrepareForCluster(PCGExClusters::FCluster* InCluster)
{
	Cluster = InCluster;
}

bool FPCGExSearchOperation::ResolveQuery(
	const TSharedPtr<PCGExPathfinding::FPathQuery>& InQuery,
	const TSharedPtr<PCGExPathfinding::FSearchAllocations>& Allocations,
	const TSharedPtr<PCGExHeuristics::FHandler>& Heuristics,
	const TSharedPtr<PCGExHeuristics::FLocalFeedbackHandler>& LocalFeedback) const
{
	return false;
}

TSharedPtr<PCGExPathfinding::FSearchAllocations> FPCGExSearchOperation::NewAllocations() const
{
	TSharedPtr<PCGExPathfinding::FSearchAllocations> Allocations = MakeShared<PCGExPathfinding::FSearchAllocations>();
	Allocations->Init(Cluster);
	return Allocations;
}

TSharedPtr<PCGExPathfinding::FSearchAllocations> FPCGExSearchOperation::PrepareAllocations(const TSharedPtr<PCGExPathfinding::FSearchAllocations>& InAllocations) const
{
	if (!InAllocations)
	{
		return NewAllocations();
	}

	InAllocations->Reset();
	return InAllocations;
}

TSharedPtr<PCGExPathfinding::FSearchAllocations> FPCGExSearchOperation::AcquireAllocations()
{
	{
		FScopeLock Lock(&AllocationsPoolLock);
		if (!AllocationsPool.IsEmpty())
		{
			return AllocationsPool.Pop();
		}
	}

	return NewAllocations();
}

void FPCGExSearchOperation::ReleaseAllocations(const TSharedPtr<PCGExPathfinding::FSearchAllocations>& InAllocations)
{
	if (!InAllocations)
	{
		return;
	}

	FScopeLock Lock(&AllocationsPoolLock);
	AllocationsPool.Add(InAllocations);
}


void UPCGExSearchInstancedFactory::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
}
