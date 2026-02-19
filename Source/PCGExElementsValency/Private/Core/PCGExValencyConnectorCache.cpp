// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExValencyConnectorCache.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExValencyBondingRules.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "Core/PCGExValencyMap.h"
#include "Data/PCGExData.h"

namespace PCGExValency
{
	bool FConnectorCache::BuildFrom(
		const TSharedPtr<PCGExClusters::FCluster>& Cluster,
		const TSharedPtr<PCGExData::TBuffer<int64>>& EdgeConnectorReader,
		const TSharedPtr<PCGExData::TBuffer<int64>>& ValencyEntryReader,
		const FPCGExValencyBondingRulesCompiled* CompiledRules,
		const UPCGExValencyConnectorSet* ConnectorSet,
		int32 InMaxTypes)
	{
		if (!Cluster || !EdgeConnectorReader || !ValencyEntryReader || !CompiledRules || !ConnectorSet || InMaxTypes <= 0)
		{
			return false;
		}

		NumNodes = Cluster->Nodes->Num();
		MaxTypes = InMaxTypes;

		if (NumNodes <= 0)
		{
			return false;
		}

		const TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;
		const TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;

		// Phase 1: Count neighbors per node per type (temporary)
		// Use a temporary 2D array: [NodeIndex][TypeIndex] -> TArray<int32> of neighbor node indices
		TArray<TArray<TArray<int32>>> TempNeighbors;
		TempNeighbors.SetNum(NumNodes);
		for (int32 i = 0; i < NumNodes; ++i)
		{
			TempNeighbors[i].SetNum(MaxTypes);
		}

		// Initialize type masks
		NodeConnectorTypeMasks.SetNumZeroed(NumNodes);

		// Process each node's links
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const PCGExClusters::FNode& Node = Nodes[NodeIndex];

			// Read this node's module index from ValencyEntry
			const int64 EntryHash = ValencyEntryReader->Read(Node.PointIndex);
			if (!EntryData::IsValid(static_cast<uint64>(EntryHash))) { continue; }

			const int32 NodeModuleIndex = EntryData::GetModuleIndex(static_cast<uint64>(EntryHash));
			if (NodeModuleIndex < 0) { continue; }

			for (const PCGExClusters::FLink& Link : Node.Links)
			{
				const int32 EdgeIndex = Link.Edge;
				const int32 NeighborNodeIndex = Link.Node;

				if (!Edges.IsValidIndex(EdgeIndex)) { continue; }

				const PCGExGraphs::FEdge& Edge = Edges[EdgeIndex];
				const int64 PackedConnectors = EdgeConnectorReader->Read(Edge.PointIndex);

				// Determine which connector instance applies to this node
				const bool bIsStart = (Edge.Start == static_cast<uint32>(Node.PointIndex));
				const int32 FlatConnectorIdx = bIsStart
					? EdgeConnector::GetSourceIndex(PackedConnectors)
					: EdgeConnector::GetTargetIndex(PackedConnectors);

				if (FlatConnectorIdx < 0 || !CompiledRules->AllModuleConnectors.IsValidIndex(FlatConnectorIdx))
				{
					continue;
				}

				// Look up the connector instance to get its type name
				const FPCGExValencyModuleConnector& ConnectorInstance = CompiledRules->AllModuleConnectors[FlatConnectorIdx];
				const int32 TypeIndex = ConnectorSet->FindConnectorTypeIndex(ConnectorInstance.ConnectorType);

				if (TypeIndex < 0 || TypeIndex >= MaxTypes)
				{
					continue;
				}

				// Record the neighbor
				TempNeighbors[NodeIndex][TypeIndex].AddUnique(NeighborNodeIndex);
				NodeConnectorTypeMasks[NodeIndex] |= (1LL << TypeIndex);
			}
		}

		// Phase 2: Flatten into cache-efficient arrays
		// Count total neighbors to pre-allocate
		int32 TotalNeighbors = 0;
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			for (int32 TypeIndex = 0; TypeIndex < MaxTypes; ++TypeIndex)
			{
				TotalNeighbors += TempNeighbors[NodeIndex][TypeIndex].Num();
			}
		}

		PerNodeTypeHeaders.SetNumUninitialized(NumNodes * MaxTypes);
		AllConnectorNeighbors.Reserve(TotalNeighbors);

		int32 CurrentIdx = 0;
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			for (int32 TypeIndex = 0; TypeIndex < MaxTypes; ++TypeIndex)
			{
				const TArray<int32>& Neighbors = TempNeighbors[NodeIndex][TypeIndex];
				const int32 Count = Neighbors.Num();

				PerNodeTypeHeaders[NodeIndex * MaxTypes + TypeIndex] = FIntPoint(CurrentIdx, Count);

				for (const int32 NeighborIdx : Neighbors)
				{
					AllConnectorNeighbors.Add(NeighborIdx);
				}

				CurrentIdx += Count;
			}
		}

		return true;
	}

	int32 FConnectorCache::GetTotalNeighborCount(int32 NodeIndex) const
	{
		int32 Total = 0;
		const int32 BaseIdx = NodeIndex * MaxTypes;
		for (int32 TypeIdx = 0; TypeIdx < MaxTypes; ++TypeIdx)
		{
			if (PerNodeTypeHeaders.IsValidIndex(BaseIdx + TypeIdx))
			{
				Total += PerNodeTypeHeaders[BaseIdx + TypeIdx].Y;
			}
		}
		return Total;
	}

	void FConnectorCache::Reset()
	{
		NumNodes = 0;
		MaxTypes = 0;
		NodeConnectorTypeMasks.Empty();
		PerNodeTypeHeaders.Empty();
		AllConnectorNeighbors.Empty();
	}
}
