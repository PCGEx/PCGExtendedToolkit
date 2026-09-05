// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineRemoveLeavesRecursive.h"

#pragma region FPCGExEdgeRemoveLeavesRecursive

void FPCGExEdgeRemoveLeavesRecursive::Process()
{
	const int32 NumNodes = Cluster->Nodes->Num();

	// Track valid link counts for each node using atomics
	TArray<std::atomic<int32>> ValidLinkCounts;
	ValidLinkCounts.SetNum(NumNodes);

	// Initialize valid link counts in parallel
	ParallelFor(NumNodes, [&](const int32 i)
	{
		const PCGExClusters::FNode& Node = (*Cluster->Nodes)[i];
		ValidLinkCounts[i].store(Node.bValid ? Node.Links.Num() : 0, std::memory_order_relaxed);
	});

	// Find initial leaves
	TArray<int32> LeafQueue;
	for (int32 i = 0; i < NumNodes; i++)
	{
		if (ValidLinkCounts[i].load(std::memory_order_relaxed) == 1)
		{
			LeafQueue.Add(i);
		}
	}

	// Track which nodes should be in the next queue
	TArray<std::atomic<bool>> QueuedForNext;
	QueuedForNext.SetNum(NumNodes);

	int32 Iterations = 0;
	while (LeafQueue.Num() > 0 && (MaxIterations <= 0 || Iterations < MaxIterations))
	{
		Iterations++;

		// Reset next queue flags
		ParallelFor(NumNodes, [&](const int32 i)
		{
			QueuedForNext[i].store(false, std::memory_order_relaxed);
		});

		ParallelFor(LeafQueue.Num(), [&](const int32 Idx)
		{
			const int32 NodeIndex = LeafQueue[Idx];
			PCGExClusters::FNode& Node = (*Cluster->Nodes)[NodeIndex];

			if (!Node.bValid)
			{
				return;
			}
			if (ValidLinkCounts[NodeIndex].load(std::memory_order_acquire) != 1)
			{
				return;
			}

			for (const PCGExGraphs::FLink& Lk : Node.Links)
			{
				PCGExGraphs::FEdge* Edge = Cluster->GetEdge(Lk.Edge);
				if (!Edge->bValid)
				{
					continue;
				}

				// Invalidate the leaf and its edge
				Node.bValid = false;
				Edge->bValid = false;

				// Atomically decrement neighbor's valid link count
				const int32 NeighborIndex = Lk.Node;
				const int32 NewCount = ValidLinkCounts[NeighborIndex].fetch_sub(1, std::memory_order_acq_rel) - 1;

				// If neighbor became a leaf, mark it for next iteration
				if (NewCount == 1)
				{
					const PCGExClusters::FNode* Neighbor = Cluster->GetNode(NeighborIndex);
					if (Neighbor->bValid)
					{
						QueuedForNext[NeighborIndex].store(true, std::memory_order_release);
					}
				}

				break;
			}
		});

		// Build next queue from flags
		LeafQueue.Reset();
		for (int32 i = 0; i < NumNodes; i++)
		{
			if (QueuedForNext[i].load(std::memory_order_acquire))
			{
				LeafQueue.Add(i);
			}
		}
	}
}

#pragma endregion

#pragma region UPCGExEdgeRemoveLeavesRecursive

void UPCGExEdgeRemoveLeavesRecursive::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExEdgeRemoveLeavesRecursive* TypedOther = Cast<UPCGExEdgeRemoveLeavesRecursive>(Other))
	{
		MaxIterations = TypedOther->MaxIterations;
	}
}

#pragma endregion
