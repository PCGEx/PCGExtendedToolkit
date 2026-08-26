// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExH.h"
#include "PCGExLink.h"
#include "PCGExEdge.generated.h"

UENUM()
enum class EPCGExEdgeDirectionMethod : uint8
{
	EndpointsOrder   = 0 UMETA(DisplayName = "Endpoints Order", ToolTip="Uses the edge' Start & End properties"),
	EndpointsIndices = 1 UMETA(DisplayName = "Endpoints Indices", ToolTip="Uses the edge' Start & End indices"),
	EndpointsSort    = 2 UMETA(DisplayName = "Endpoints Sort", ToolTip="Uses sorting rules to check endpoint is the Start or End."),
	EdgeDotAttribute = 3 UMETA(DisplayName = "Edge Dot Attribute", ToolTip="Chooses the highest dot product against a vector property or attribute on the edge point"),
};

UENUM()
enum class EPCGExEdgeDirectionChoice : uint8
{
	SmallestToGreatest = 0 UMETA(DisplayName = "Smallest to Greatest", ToolTip="Endpoints are ordered smallest to greatest; the resulting direction vector points AT the smallest-value endpoint."),
	GreatestToSmallest = 1 UMETA(DisplayName = "Greatest to Smallest", ToolTip="Endpoints are ordered greatest to smallest; the resulting direction vector points AT the greatest-value endpoint.")
};

namespace PCGExGraphs
{
	struct PCGEXCORE_API FEdge
	{
		uint32 Start = 0;
		uint32 End = 0;
		int32 Index = -1;
		int32 PointIndex = -1;
		int32 IOIndex = -1;
		int8 bValid = 1;

		constexpr FEdge() = default;

		constexpr FEdge(const int32 InIndex, const uint32 InStart, const uint32 InEnd, const int32 InPointIndex = -1, const int32 InIOIndex = -1)
			: Start(InStart)
			  , End(InEnd)
			  , Index(InIndex)
			  , PointIndex(InPointIndex)
			  , IOIndex(InIOIndex)
		{
		}

		FORCEINLINE uint32 Other(const int32 InIndex) const
		{
			check(InIndex == Start || InIndex == End)
			return InIndex == Start ? End : Start;
		}

		FORCEINLINE bool Contains(const int32 InIndex) const
		{
			return Start == InIndex || End == InIndex;
		}

		bool operator==(const FEdge& Other) const
		{
			return PCGEx::H64U(Start, End) == PCGEx::H64U(Other.Start, Other.End);
		}

		FORCEINLINE uint64 H64U() const
		{
			return PCGEx::H64U(Start, End);
		}

		FORCEINLINE uint32 GetTypeHash(const FLink& Key)
		{
			return HashCombineFast(Key.Node, Key.Edge);
		}

		FORCEINLINE bool operator<(const FEdge& Other) const
		{
			return H64U() < Other.H64U();
		}
	};
}

namespace PCGExClusters
{
	class FCluster;

	struct PCGEXCORE_API FBoundedEdge
	{
		int32 Index;
		FBoxSphereBounds Bounds;

		FBoundedEdge(const FCluster* Cluster, const int32 InEdgeIndex);
		FBoundedEdge();

		~FBoundedEdge() = default;

		bool operator==(const FBoundedEdge& ExpandedEdge) const
		{
			return (Index == ExpandedEdge.Index && Bounds == ExpandedEdge.Bounds);
		};
	};
}
