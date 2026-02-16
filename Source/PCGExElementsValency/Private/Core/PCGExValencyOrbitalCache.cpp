// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExValencyOrbitalCache.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExValencyOrbitalSet.h"
#include "Data/PCGExData.h"

namespace PCGExValency
{
	bool FOrbitalCache::BuildFrom(
		const TSharedPtr<PCGExClusters::FCluster>& Cluster,
		const TSharedPtr<PCGExData::TBuffer<int64>>& OrbitalMaskReader,
		const TSharedPtr<PCGExData::TBuffer<int64>>& EdgeIndicesReader,
		int32 InMaxOrbitals)
	{
		if (!Cluster || !OrbitalMaskReader || !EdgeIndicesReader || InMaxOrbitals <= 0)
		{
			return false;
		}

		NumNodes = Cluster->Nodes->Num();
		MaxOrbitals = InMaxOrbitals;

		if (NumNodes <= 0)
		{
			return false;
		}

		// Allocate flat arrays
		NodeOrbitalMasks.SetNumUninitialized(NumNodes);
		FlatOrbitalToNeighbor.SetNumUninitialized(NumNodes * MaxOrbitals);

		// Initialize all neighbors to -1 (no neighbor)
		FMemory::Memset(FlatOrbitalToNeighbor.GetData(), 0xFF, FlatOrbitalToNeighbor.Num() * sizeof(int32));

		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;

		// Build cache for each node (parallelized - each node writes to its own slice)
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const PCGExClusters::FNode& Node = Nodes[NodeIndex];

			// Read and store orbital mask from vertex attribute
			NodeOrbitalMasks[NodeIndex] = OrbitalMaskReader->Read(Node.PointIndex);

			// Build orbital-to-neighbor from edge indices
			for (const PCGExClusters::FLink& Link : Node.Links)
			{
				const int32 EdgeIndex = Link.Edge;
				const int32 NeighborNodeIndex = Link.Node;

				if (!Edges.IsValidIndex(EdgeIndex)) { continue; }

				const PCGExGraphs::FEdge& Edge = Edges[EdgeIndex];
				const int64 PackedIndices = EdgeIndicesReader->Read(EdgeIndex);

				// Determine which orbital index applies to this node
				const uint8 OrbitalIndex = (Edge.Start == static_cast<uint32>(Node.PointIndex))
					                           ? PCGExValency::EdgeOrbital::GetStartOrbital(PackedIndices)
					                           : PCGExValency::EdgeOrbital::GetEndOrbital(PackedIndices);

				// Skip if no match (sentinel value)
				if (OrbitalIndex == NO_ORBITAL_MATCH) { continue; }

				// Store neighbor at this orbital
				if (OrbitalIndex < MaxOrbitals)
				{
					FlatOrbitalToNeighbor[NodeIndex * MaxOrbitals + OrbitalIndex] = NeighborNodeIndex;
				}
			}
		}

		return true;
	}

	bool FOrbitalCache::BuildFromArrays(
		const TSharedPtr<PCGExClusters::FCluster>& Cluster,
		const TArray<int64>& VertexOrbitalMasks,
		const TArray<int64>& EdgePackedIndices,
		int32 InMaxOrbitals)
	{
		if (!Cluster || InMaxOrbitals <= 0)
		{
			return false;
		}

		NumNodes = Cluster->Nodes->Num();
		MaxOrbitals = InMaxOrbitals;

		if (NumNodes <= 0)
		{
			return false;
		}

		// Allocate flat arrays
		NodeOrbitalMasks.SetNumUninitialized(NumNodes);
		FlatOrbitalToNeighbor.SetNumUninitialized(NumNodes * MaxOrbitals);

		// Initialize all neighbors to -1 (no neighbor)
		FMemory::Memset(FlatOrbitalToNeighbor.GetData(), 0xFF, FlatOrbitalToNeighbor.Num() * sizeof(int32));

		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;

		// Build cache for each node
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const PCGExClusters::FNode& Node = Nodes[NodeIndex];

			// Read orbital mask from raw array (indexed by PointIndex)
			NodeOrbitalMasks[NodeIndex] = VertexOrbitalMasks.IsValidIndex(Node.PointIndex)
				? VertexOrbitalMasks[Node.PointIndex]
				: 0;

			// Build orbital-to-neighbor from edge indices
			for (const PCGExClusters::FLink& Link : Node.Links)
			{
				const int32 EdgeIndex = Link.Edge;
				const int32 NeighborNodeIndex = Link.Node;

				if (!Edges.IsValidIndex(EdgeIndex) || !EdgePackedIndices.IsValidIndex(EdgeIndex)) { continue; }

				const PCGExGraphs::FEdge& Edge = Edges[EdgeIndex];
				const int64 PackedIndices = EdgePackedIndices[EdgeIndex];

				// Determine which orbital index applies to this node
				const uint8 OrbitalIndex = (Edge.Start == static_cast<uint32>(Node.PointIndex))
					                           ? PCGExValency::EdgeOrbital::GetStartOrbital(PackedIndices)
					                           : PCGExValency::EdgeOrbital::GetEndOrbital(PackedIndices);

				// Skip if no match (sentinel value)
				if (OrbitalIndex == NO_ORBITAL_MATCH) { continue; }

				// Store neighbor at this orbital
				if (OrbitalIndex < MaxOrbitals)
				{
					FlatOrbitalToNeighbor[NodeIndex * MaxOrbitals + OrbitalIndex] = NeighborNodeIndex;
				}
			}
		}

		return true;
	}

	void FOrbitalCache::InitializeStates(TArray<FValencyState>& OutStates) const
	{
		OutStates.SetNum(NumNodes);

		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			OutStates[NodeIndex].NodeIndex = NodeIndex;
			OutStates[NodeIndex].ResolvedModule = SlotState::UNSET;
		}
	}

	void FOrbitalCache::Reset()
	{
		NumNodes = 0;
		MaxOrbitals = 0;
		NodeOrbitalMasks.Empty();
		FlatOrbitalToNeighbor.Empty();
	}
}
