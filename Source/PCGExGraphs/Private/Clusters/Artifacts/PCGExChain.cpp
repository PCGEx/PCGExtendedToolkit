// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExChain.h"

#include "Algo/RemoveIf.h"
#include "Clusters/PCGExCluster.h"
#include "Data/PCGExPointIO.h"

namespace PCGExClusters
{
	void FNodeChain::FixUniqueHash()
	{
		UniqueHash = 0;

		if (Links.Num() <= 1)
		{
			SingleEdge = Seed.Edge;
			UniqueHash = SingleEdge;
			return;
		}

		if (bIsClosedLoop)
		{
			// A closed loop seeded at Seed.Node uses two of that node's edges: the initial edge
			// (preserved as Links[0].Edge) and the closing edge (Seed.Edge after BuildChain wraps).
			// Reverse-direction traversal swaps which is initial vs closing, but the unordered
			// pair is identical; so key on it to dedupe both representations.
			const FLink& First = Links[0];
			UniqueHash = PCGEx::H64U(
				HashCombineFast(Seed.Node, FMath::Min(Seed.Edge, First.Edge)),
				HashCombineFast(Seed.Node, FMath::Max(Seed.Edge, First.Edge)));
			return;
		}

		const FLink LastLink = Links.Last();
		UniqueHash = PCGEx::H64U(HashCombineFast(Seed.Node, Seed.Edge), HashCombineFast(LastLink.Node, LastLink.Edge));
	}

	void FNodeChain::BuildChain(const TSharedRef<FCluster>& Cluster, const TSharedPtr<TArray<int8>>& Breakpoints)
	{
		ON_SCOPE_EXIT
		{
			bIsLeaf = Cluster->GetNode(Seed.Node)->IsLeaf() || Cluster->GetNode(Links.Last().Node)->IsLeaf();
			if (bIsClosedLoop)
			{
				bIsLeaf = false;
			}
			FixUniqueHash();
		};

		// Note: No TSet needed for cycle detection. Binary nodes have exactly 2 neighbors,
		// so walking through them can't revisit nodes - we came from one, go to the other.
		// Closed loops are detected by checking if we return to Seed.Node.

		FLink Last = Seed;
		FNode* FromNode = Cluster->GetEdgeOtherNode(Seed);
		Links.Add(FLink(FromNode->Index, Seed.Edge));

		while (FromNode)
		{
			if (FromNode->IsLeaf() || FromNode->IsComplex() || (Breakpoints && (*Breakpoints)[FromNode->PointIndex]))
			{
				bIsClosedLoop = false;
				break;
			}

			FLink NextLink = FromNode->Links[0]; // Get next node
			if (NextLink.Node == Last.Node)
			{
				NextLink = FromNode->Links[1];
			} // Get other next

			if (NextLink.Node == Seed.Node)
			{
				Seed.Edge = NextLink.Edge; // !
				bIsClosedLoop = true;
				break;
			}

			Last = Links.Last();
			Links.Add(NextLink);

			FromNode = Cluster->GetNode(NextLink.Node);
		}
	}

	FVector FNodeChain::GetFirstEdgeDir(const TSharedPtr<FCluster>& Cluster) const
	{
		return Cluster->GetDir(Seed.Node, (*Cluster->NodeIndexLookup)[Cluster->GetEdge(Seed.Edge)->Other(Cluster->GetNodePointIndex(Seed.Node))]);
	}

	FVector FNodeChain::GetLastEdgeDir(const TSharedPtr<FCluster>& Cluster) const
	{
		const FLink& Lk = Links.Last();
		return Cluster->GetDir(Lk.Node, (*Cluster->NodeIndexLookup)[Cluster->GetEdge(Lk.Edge)->Other(Cluster->GetNodePointIndex(Lk.Node))]);
	}

	FVector FNodeChain::GetEdgeDir(const TSharedPtr<FCluster>& Cluster, const bool bFirst) const
	{
		if (bFirst)
		{
			return GetFirstEdgeDir(Cluster);
		}
		return GetLastEdgeDir(Cluster);
	}

	FVector FNodeChain::GetOutwardDirAt(const TSharedPtr<FCluster>& Cluster, const int32 NodeIndex, const bool bExitSide) const
	{
		const bool bAtSeed = (NodeIndex == Seed.Node);
		const bool bAtEnd = (NodeIndex == Links.Last().Node);

		// Closed-loop seed: Seed.Edge is the closing edge (overwritten during BuildChain), so we
		// can't go through GetFirstEdgeDir. Resolve directly via Links[0]/Last() neighbor positions.
		if (bIsClosedLoop && bAtSeed && !Links.IsEmpty())
		{
			return bExitSide
				? Cluster->GetDir(Seed.Node, Links[0].Node)
				: Cluster->GetDir(Seed.Node, Links.Last().Node);
		}

		return GetEdgeDir(Cluster, (bAtSeed && bAtEnd) ? bExitSide : bAtSeed);
	}

	int32 FNodeChain::GetNodes(const TSharedPtr<FCluster>& Cluster, TArray<int32>& OutNodes, const bool bReverse)
	{
		if (SingleEdge != -1)
		{
			OutNodes.Reset(2);
			FEdge* Edge = Cluster->GetEdge(SingleEdge);

			if (bReverse)
			{
				OutNodes.Add(Cluster->GetNode((*Cluster->NodeIndexLookup)[Edge->End])->Index);
				OutNodes.Add(Cluster->GetNode((*Cluster->NodeIndexLookup)[Edge->Start])->Index);
			}
			else
			{
				OutNodes.Add(Cluster->GetNode((*Cluster->NodeIndexLookup)[Edge->Start])->Index);
				OutNodes.Add(Cluster->GetNode((*Cluster->NodeIndexLookup)[Edge->End])->Index);
			}

			return 2;
		}

		const int32 ChainSize = Links.Num();

		OutNodes.Reset(ChainSize + 1);

		if (bReverse)
		{
			for (int i = ChainSize - 1; i >= 0; i--)
			{
				OutNodes.Add(Links[i].Node);
			}
			OutNodes.Add(Seed.Node);
		}
		else
		{
			OutNodes.Add(Seed.Node);
			for (int i = 0; i < ChainSize; i++)
			{
				OutNodes.Add(Links[i].Node);
			}
		}

		return OutNodes.Num();
	}
}
