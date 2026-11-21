// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExGraphMetadata.generated.h"

struct FPCGExPointEdgeIntersectionDetails;
struct FPCGExEdgeEdgeIntersectionDetails;
struct FPCGExPointPointIntersectionDetails;
struct FPCGExEdgeUnionMetadataDetails;
struct FPCGExPointUnionMetadataDetails;
struct FPCGExContext;
struct FPCGExCarryOverDetails;
struct FPCGExBlendingDetails;

UENUM()
enum class EPCGExIntersectionType : uint8
{
	Unknown   = 0 UMETA(DisplayName = "Unknown", ToolTip="Unknown"),
	PointEdge = 1 UMETA(DisplayName = "Point/Edge", ToolTip="Point/Edge Intersection."),
	EdgeEdge  = 2 UMETA(DisplayName = "Edge/Edge", ToolTip="Edge/Edge Intersection."),
	FusedEdge = 3 UMETA(DisplayName = "Fused Edge", ToolTip="Fused Edge Intersection."),
};

namespace PCGExGraphs
{
	struct PCGEXGRAPHS_API FGraphMetadataDetails
	{
		const FPCGExBlendingDetails* EdgesBlendingDetailsPtr = nullptr;
		const FPCGExCarryOverDetails* EdgesCarryOverDetails = nullptr;

		bool bWriteIsPointUnion = false;
		FName IsPointUnionAttributeName = FName("bIsUnion");

		bool bWritePointUnionSize = false;
		FName PointUnionSizeAttributeName = FName("UnionSize");

		bool bWriteIsSubEdge = false;
		FName IsSubEdgeAttributeName = FName("bIsSubEdge");

		bool bWriteIsEdgeUnion = false;
		FName IsEdgeUnionAttributeName = FName("bIsUnion");

		bool bWriteEdgeUnionSize = false;
		FName EdgeUnionSizeAttributeName = FName("UnionSize");

		bool bWriteIsIntersector = false;
		FName IsIntersectorAttributeName = FName("bIsIntersector");

		bool bWriteCrossing = false;
		FName CrossingAttributeName = FName("bCrossing");

		bool bFlagCrossing = false;
		FName FlagA = NAME_None;
		FName FlagB = NAME_None;

		void Update(FPCGExContext* InContext, const FPCGExPointUnionMetadataDetails& InDetails);
		void Update(FPCGExContext* InContext, const FPCGExEdgeUnionMetadataDetails& InDetails);
		void Update(FPCGExContext* InContext, const FPCGExPointPointIntersectionDetails& InDetails);
		void Update(FPCGExContext* InContext, const FPCGExPointEdgeIntersectionDetails& InDetails);
		void Update(FPCGExContext* InContext, const FPCGExEdgeEdgeIntersectionDetails& InDetails);
	};

	struct PCGEXGRAPHS_API FGraphNodeMetadata
	{
		int32 NodeIndex = -1;
		int32 UnionSize = 0; // Fuse size
		EPCGExIntersectionType Type = EPCGExIntersectionType::Unknown;

		FGraphNodeMetadata() = default;
		explicit FGraphNodeMetadata(const int32 InNodeIndex, const EPCGExIntersectionType InType = EPCGExIntersectionType::Unknown);

		FORCEINLINE bool IsUnion() const
		{
			return UnionSize > 1;
		}

		FORCEINLINE bool IsIntersector() const
		{
			return Type == EPCGExIntersectionType::PointEdge;
		}

		FORCEINLINE bool IsCrossing() const
		{
			return Type == EPCGExIntersectionType::EdgeEdge;
		}
	};

	struct PCGEXGRAPHS_API FGraphEdgeMetadata
	{
		int32 EdgeIndex = -1;
		int32 RootIndex = -1;
		EPCGExIntersectionType Type = EPCGExIntersectionType::Unknown;

		int32 UnionSize = 0; // Fuse size
		int8 bIsSubEdge = 0; // Sub Edge (result of a)

		FORCEINLINE bool IsUnion() const
		{
			return UnionSize > 1;
		}

		FORCEINLINE bool IsRoot() const
		{
			return EdgeIndex == RootIndex;
		}

		FGraphEdgeMetadata() = default;
		explicit FGraphEdgeMetadata(const int32 InEdgeIndex, const int32 InRootIndex = -1, const EPCGExIntersectionType InType = EPCGExIntersectionType::Unknown);
	};
}
