// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExH.h"
#include "PCGExLink.h"

namespace PCGExGraphs
{
	FORCEINLINE static uint32 NodeGUID(const uint32 Base, const int32 Index)
	{
		uint32 A;
		uint32 B;
		PCGEx::H64(Base, A, B);
		return HashCombineFast(A == 0 ? B : A, Index);
	}

	struct PCGEXCORE_API FNode
	{
		FNode() = default;

		FNode(const int32 InNodeIndex, const int32 InPointIndex);

		int8 bValid = 1; // int for atomic operations

		int32 Index = -1;      // Index in the context of the list that helds the node
		int32 PointIndex = -1; // Index in the context of the UPCGBasePointData that helds the vtx
		int32 NumExportedEdges = 0;

		NodeLinks Links;

		~FNode() = default;

		FORCEINLINE int32 Num() const
		{
			return Links.Num();
		}

		FORCEINLINE int32 IsEmpty() const
		{
			return Links.IsEmpty();
		}

		FORCEINLINE bool IsLeaf() const
		{
			return Links.Num() == 1;
		}

		FORCEINLINE bool IsBinary() const
		{
			return Links.Num() == 2;
		}

		FORCEINLINE bool IsComplex() const
		{
			return Links.Num() > 2;
		}

		FORCEINLINE void LinkEdge(const int32 EdgeIndex)
		{
			Links.AddUnique(FLink(0, EdgeIndex));
		}

		// Plain append: builders visit each edge index exactly once per endpoint, so
		// (node, edge) pairs are unique by construction. FLink equality includes the
		// edge index, so a dedup scan could never reject anything a caller produces;
		// degenerate edges are rejected at graph insertion, not at link level.
		FORCEINLINE void Link(const int32 NodeIndex, const int32 EdgeIndex)
		{
			Links.Add(FLink(NodeIndex, EdgeIndex));
		}

		bool IsAdjacentTo(const int32 OtherNodeIndex) const;

		int32 GetEdgeIndex(const int32 AdjacentNodeIndex) const;
	};
}

namespace PCGExClusters
{
	class FCluster;

	struct PCGEXCORE_API FAdjacencyData
	{
		int32 NodeIndex = -1;
		int32 NodePointIndex = -1;
		int32 EdgeIndex = -1;
		FVector Direction = FVector::OneVector;
		double Length = 0;
	};

	struct PCGEXCORE_API FNode : PCGExGraphs::FNode
	{
		FNode() = default;

		FNode(const int32 InNodeIndex, const int32 InPointIndex);

		FVector GetCentroid(const FCluster* InCluster) const;
		int32 ValidEdges(const FCluster* InCluster);
		bool HasAnyValidEdges(const FCluster* InCluster);
	};
}
