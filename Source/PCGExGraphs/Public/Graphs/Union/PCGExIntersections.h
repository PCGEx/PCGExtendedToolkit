// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExOctree.h"
#include "Core/PCGExOpStats.h"

#include "Clusters/PCGExEdge.h"
#include "Data/PCGExPointElements.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Utils/PCGValueRange.h"

struct FPCGExEdgeEdgeIntersectionDetails;
struct FPCGExPointEdgeIntersectionDetails;

namespace PCGExBlending
{
	class FMetadataBlender;
}

namespace PCGExData
{
	class FUnionMetadata;
	class IUnionMetadata;
	class IUnionData;
}

namespace PCGExGraphs
{
	class FGraph;
	struct FEdge;

	// Per-graph scratch shared by the P/E and E/E passes. Holds everything that's expensive to
	// recompute from FGraph: node positions, per-edge length/direction/AABB, validity bitmap,
	// pre-resolved root metadata indices, and (lazily) the edge octree for E/E.
	//
	// Lifecycle: built once per cluster build via Build(). Self-intersection callers additionally
	// call BuildRootIOSets(). E/E callers additionally call BuildEdgeOctree(). Subsequent passes
	// read it concurrently and treat it as immutable.
	class PCGEXGRAPHS_API FIntersectionAllocations : public TSharedFromThis<FIntersectionAllocations>
	{
	public:
		TSharedPtr<FGraph> Graph;
		TSharedPtr<PCGExData::FPointIO> PointIO;

		TConstPCGValueRange<FTransform> NodeTransforms;

		// Per-node (size NumNodes)
		TArray<FVector> Positions;

		// Per-edge (size NumEdges)
		TBitArray<> ValidEdges;
		TArray<double> LengthSquared;
		TArray<FVector> Directions;
		TArray<FBox> EdgeBoxes;

		// Per-edge metadata flat-lookup. Replaces TMap-based FindEdgeMetadata_Unsafe in hot loops.
		// EdgeRootIndex[i] is the root-edge metadata index for edge i (-1 when no metadata exists).
		// Only populated for valid (non-degenerate) edges; invalid edges carry a sentinel that is
		// never read by Apply (which skips empty buckets).
		TArray<int32> EdgeRootIndex;

		// IO-set deduplication for self-intersection filtering. EdgeRootIOSetIdx[i] is the index
		// into UniqueRootIOSets for edge i (-1 when no set was built, e.g. invalid edge or
		// EdgesUnion is null). UniqueRootIOSets holds one entry per unique root index, so the
		// total heap allocation is O(M) instead of O(N) for N edges sharing M unique roots.
		// Only populated when BuildRootIOSets() is called (i.e. self-intersection is disabled).
		TArray<int32> EdgeRootIOSetIdx;
		TArray<TSet<int32>> UniqueRootIOSets;

		// E/E only.
		TSharedPtr<PCGExOctree::FItemOctree> EdgeOctree;

		double Tolerance = 10;
		double ToleranceSquared = 100;

		FIntersectionAllocations(const TSharedPtr<FGraph>& InGraph, const TSharedPtr<PCGExData::FPointIO>& InPointIO);
		~FIntersectionAllocations() = default;

		// Builds per-node Positions, per-edge cache + EdgeRootIndex. Octree and IO sets opt-in.
		void Build(double InTolerance);

		// Populates EdgeRootIOSetIdx / UniqueRootIOSets for self-intersection filtering. Call after Build().
		void BuildRootIOSets();

		// Builds the edge octree. Call after Build(). Bounds is the world-space envelope plus the
		// tolerance margin -- typically the full graph bounds.
		void BuildEdgeOctree(const FBox& InBounds);
	};

#pragma region Point Edge intersections

	// Single hit emitted by the P/E find pass. Sorted by (EdgeIdx, Time) before apply.
	struct PCGEXGRAPHS_API FPECollinear
	{
		int32 EdgeIdx = -1; // edge being split
		int32 NodeIdx = -1; // collinear graph node
		double Time = -1;   // 0..1 along the edge
		FVector ClosestPoint = FVector::ZeroVector;

		FPECollinear() = default;

		FPECollinear(const int32 InEdgeIdx, const int32 InNodeIdx, const double InTime, const FVector& InClosest)
			: EdgeIdx(InEdgeIdx)
			  , NodeIdx(InNodeIdx)
			  , Time(InTime)
			  , ClosestPoint(InClosest)
		{
		}
	};

	namespace PointEdgePass
	{
		// Phase 1 -- parallel emit. Scans edges in [Scope.Start, Scope.Start + Scope.Count) and
		// appends collinear-node hits to OutScopeRecords. Lock-free per scope. Marks split edges
		// invalid in Allocations.ValidEdges via atomic store (so subsequent E/E sees the updated
		// set without an apply step).
		PCGEXGRAPHS_API void Emit(
			FIntersectionAllocations& Allocations,
			const FPCGExPointEdgeIntersectionDetails& Details,
			bool bEnableSelfIntersection,
			const PCGExMT::FScope& Scope,
			TArray<FPECollinear>& OutScopeRecords);

		// Phase 2 -- sequential apply. Sorts by (EdgeIdx, Time), then walks per-edge subdivisions
		// and mutates the Graph (insert sub-edges, stamp metadata). bSnapOnEdge mutates output
		// transforms when true.
		PCGEXGRAPHS_API void Apply(
			FIntersectionAllocations& Allocations,
			const FPCGExPointEdgeIntersectionDetails& Details,
			TArray<FPECollinear>& Records);

		// TODO: proper P/E blend (lerp-weight). The call site is a stub; kept next to its sibling phases.
		PCGEXGRAPHS_API void BlendIntersection(
			const FIntersectionAllocations& Allocations,
			PCGExBlending::FMetadataBlender* Blender,
			const FPECollinear& Record);
	}

#pragma endregion

#pragma region Edge Edge intersections

	// Single crossing emitted by the E/E find pass. Canonical ordering EdgeA < EdgeB.
	// ResolvedNodeIdx is filled by the dedup pass: either reused from a nearby existing node or
	// pointing at a freshly-allocated node. Dropped entries (duplicates that resolve to the same
	// already-allocated crossing) carry the same ResolvedNodeIdx as their primary.
	struct PCGEXGRAPHS_API FEECrossing
	{
		int32 EdgeA = -1;
		int32 EdgeB = -1;
		double TimeA = -1;
		double TimeB = -1;
		FVector Center = FVector::ZeroVector;

		int32 ResolvedNodeIdx = INDEX_NONE;
		bool bIsPrimary = false;        // true for the canonical record per (EdgeA, EdgeB) key
		bool bAllocatedNewNode = false; // true when ResolvedNodeIdx points at a freshly added node

		FEECrossing() = default;

		FORCEINLINE uint64 Key() const
		{
			return PCGEx::H64U(EdgeA, EdgeB);
		}

		FORCEINLINE double GetTime(const int32 EdgeIdx) const
		{
			return EdgeIdx == EdgeA ? TimeA : TimeB;
		}
	};

	namespace EdgeEdgePass
	{
		// Phase 1 -- parallel emit. Scans edges and appends crossings (canonical EdgeA<EdgeB) to
		// OutScopeRecords. Lock-free per scope.
		PCGEXGRAPHS_API void Emit(
			FIntersectionAllocations& Allocations,
			const FPCGExEdgeEdgeIntersectionDetails& Details,
			bool bEnableSelfIntersection,
			const PCGExMT::FScope& Scope,
			TArray<FEECrossing>& OutScopeRecords);

		// Phase 2 -- sequential dedup + node allocation. Sorts records by (Key, TimeA, TimeB) for
		// determinism, collapses duplicates, runs endpoint-reuse + octree near-miss check, and
		// allocates new graph nodes for unique unresolved crossings. Fills ResolvedNodeIdx on
		// every record. Returns the number of newly-allocated nodes.
		PCGEXGRAPHS_API int32 ResolveCrossings(
			FIntersectionAllocations& Allocations,
			TArray<FEECrossing>& Records);

		// Phase 3 -- sequential apply. Re-sorts records by (EdgeIdx, Time) per edge and walks
		// subdivisions. Records must have ResolvedNodeIdx populated.
		PCGEXGRAPHS_API void Apply(
			FIntersectionAllocations& Allocations,
			TArray<FEECrossing>& Records);

		// Per-crossing blend (parallel-friendly when called over disjoint Indices).
		PCGEXGRAPHS_API void BlendIntersection(
			const FIntersectionAllocations& Allocations,
			const TSharedRef<PCGExBlending::FMetadataBlender>& Blender,
			const FEECrossing& Crossing,
			TArray<PCGEx::FOpStats>& Trackers);
	}

#pragma endregion
}
