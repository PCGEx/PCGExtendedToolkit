// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExLink.h"

namespace PCGExClusters
{
	class FCluster;
	using PCGExGraphs::FLink;

	class PCGEXGRAPHS_API FNodeChain : public TSharedFromThis<FNodeChain>
	{
	public:
		FLink Seed;
		int32 SingleEdge = -1;

		bool bIsClosedLoop = false;
		bool bIsLeaf = false;

		uint64 UniqueHash = 0;
		TArray<FLink> Links; // {Seed} [Edge <- Node][Edge <- Node]
		// Seed.Edge holds the initial edge from Seed.Node; for closed loops it is overwritten to
		// the closing edge during BuildChain. Links[0].Edge always preserves the original initial
		// edge (added before the wrap), so (Seed.Edge, Links[0].Edge) is the pair of edges-at-seed
		// participating in the loop.

		explicit FNodeChain(const FLink InSeed)
			: Seed(InSeed)
		{
		}

		~FNodeChain() = default;
		void FixUniqueHash();

		void BuildChain(const TSharedRef<FCluster>& Cluster, const TSharedPtr<TArray<int8>>& Breakpoints);

		FVector GetFirstEdgeDir(const TSharedPtr<FCluster>& Cluster) const;
		FVector GetLastEdgeDir(const TSharedPtr<FCluster>& Cluster) const;
		FVector GetEdgeDir(const TSharedPtr<FCluster>& Cluster, const bool bFirst) const;

		/** Direction at chain endpoint NodeIndex pointing outward (away from polygon, into the chain).
		 *  For closed loops at seed (Seed.Node == Links.Last().Node), bExitSide picks the opening edge (true) vs the closing edge (false).
		 *  For open chains where Seed.Node == Links.Last().Node (single-edge chains), bExitSide picks first vs last. */
		FVector GetOutwardDirAt(const TSharedPtr<FCluster>& Cluster, int32 NodeIndex, bool bExitSide) const;

		int32 GetNodes(const TSharedPtr<FCluster>& Cluster, TArray<int32>& OutNodes, bool bReverse);
	};
}
