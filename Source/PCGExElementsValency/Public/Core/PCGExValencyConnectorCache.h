// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExValencyCommon.h"

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

struct FPCGExValencyBondingRulesCompiled;
class UPCGExValencyConnectorSet;

namespace PCGExValency
{
	/**
	 * Cached connector-type-to-neighbor mappings for a cluster.
	 * Built from cluster topology + edge connector attributes.
	 *
	 * Key difference from FOrbitalCache: connector types support multi-spawn,
	 * so one type can have multiple neighbors. Lookups return arrays, not single values.
	 *
	 * Storage uses flattened arrays for cache-efficient access:
	 * - NodeConnectorTypeMasks[NodeIndex] -> int64 bitmask (bit N = has type N)
	 * - PerNodeTypeHeaders[NodeIndex * MaxTypes + TypeIndex] -> FIntPoint(StartIdx, Count)
	 * - AllConnectorNeighbors[StartIdx..StartIdx+Count-1] -> neighbor node indices
	 */
	class PCGEXELEMENTSVALENCY_API FConnectorCache : public TSharedFromThis<FConnectorCache>
	{
	public:
		FConnectorCache() = default;
		~FConnectorCache() = default;

		/**
		 * Build cache from cluster data, edge connector attributes, and compiled rules.
		 *
		 * Algorithm:
		 * 1. For each edge, read packed EdgeConnector (source/target instance indices)
		 * 2. Look up instance in CompiledRules->AllModuleConnectors[FlatIdx] -> get ConnectorType name
		 * 3. Resolve name to type index via ConnectorSet->FindConnectorTypeIndex()
		 * 4. Populate per-node-per-type neighbor lists, then flatten
		 *
		 * @param Cluster The cluster providing topology (nodes, edges, links)
		 * @param EdgeConnectorReader Edge attribute reader for packed connector instance indices
		 * @param ValencyEntryReader Vertex attribute reader for ValencyEntry (module index extraction)
		 * @param CompiledRules Compiled bonding rules (provides AllModuleConnectors)
		 * @param ConnectorSet Connector set defining available types
		 * @param InMaxTypes Maximum number of connector types to index
		 * @return True if cache was built successfully
		 */
		bool BuildFrom(
			const TSharedPtr<PCGExClusters::FCluster>& Cluster,
			const TSharedPtr<PCGExData::TBuffer<int64>>& EdgeConnectorReader,
			const TSharedPtr<PCGExData::TBuffer<int64>>& ValencyEntryReader,
			const FPCGExValencyBondingRulesCompiled* CompiledRules,
			const UPCGExValencyConnectorSet* ConnectorSet,
			int32 InMaxTypes);

		/** Get all neighbors connected via a specific connector type */
		FORCEINLINE TConstArrayView<int32> GetNeighborsAtType(int32 NodeIndex, int32 TypeIndex) const
		{
			const int32 HeaderIdx = NodeIndex * MaxTypes + TypeIndex;
			if (!PerNodeTypeHeaders.IsValidIndex(HeaderIdx)) { return TConstArrayView<int32>(); }

			const FIntPoint& Header = PerNodeTypeHeaders[HeaderIdx];
			if (Header.Y == 0) { return TConstArrayView<int32>(); }

			return TConstArrayView<int32>(&AllConnectorNeighbors[Header.X], Header.Y);
		}

		/** Check if a node has any edges via a specific connector type */
		FORCEINLINE bool HasConnectorType(int32 NodeIndex, int32 TypeIndex) const
		{
			return (NodeConnectorTypeMasks[NodeIndex] & (1LL << TypeIndex)) != 0;
		}

		/** Get the full connector type bitmask for a node */
		FORCEINLINE int64 GetConnectorTypeMask(int32 NodeIndex) const
		{
			return NodeConnectorTypeMasks[NodeIndex];
		}

		/** Get neighbor count for a specific type on a node */
		FORCEINLINE int32 GetNeighborCountAtType(int32 NodeIndex, int32 TypeIndex) const
		{
			const int32 HeaderIdx = NodeIndex * MaxTypes + TypeIndex;
			return PerNodeTypeHeaders.IsValidIndex(HeaderIdx) ? PerNodeTypeHeaders[HeaderIdx].Y : 0;
		}

		/** Get total neighbor count across all types for a node */
		int32 GetTotalNeighborCount(int32 NodeIndex) const;

		/** Get node count */
		FORCEINLINE int32 GetNumNodes() const { return NumNodes; }

		/** Get max type count */
		FORCEINLINE int32 GetMaxTypes() const { return MaxTypes; }

		/** Check if cache is valid and ready to use */
		FORCEINLINE bool IsValid() const { return NumNodes > 0 && MaxTypes > 0; }

		/** Clear the cache */
		void Reset();

	private:
		int32 NumNodes = 0;
		int32 MaxTypes = 0;

		/** Connector type bitmask per node: [NodeIndex] -> int64 bitmask */
		TArray<int64> NodeConnectorTypeMasks;

		/**
		 * Per-node-per-type headers: [NodeIndex * MaxTypes + TypeIndex] -> FIntPoint(StartIdx, Count)
		 * StartIdx = index into AllConnectorNeighbors
		 * Count = number of neighbors of this type
		 */
		TArray<FIntPoint> PerNodeTypeHeaders;

		/** Flattened array of all neighbor node indices, organized by node+type */
		TArray<int32> AllConnectorNeighbors;
	};
}
