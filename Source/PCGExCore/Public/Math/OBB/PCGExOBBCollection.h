// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExOBB.h"
#include "PCGExOBBIntersections.h"
#include "PCGExOBBTests.h"
#include "PCGExOctree.h"
#include "Math/PCGExMathBounds.h"

namespace PCGExData
{
	class FPointIO;
}

namespace PCGExMath::OBB
{
	/**
	 * Collection of OBBs with spatial indexing.
	 * TODO : Supersede BoundsCloud
	 */
	class PCGEXCORE_API FCollection
	{
	public:
		int32 CloudIndex = -1; // For intersection tracking

	protected:
		// Hot data - contiguous for spatial queries
		TArray<FBounds> Bounds;

		// Cold data - only accessed after spatial culling
		TArray<FOrientation> Orientations;

		TUniquePtr<PCGExOctree::FItemOctree> Octree;
		FBox WorldBounds = FBox(ForceInit);

		// Shared skeleton for the FindFirst-style octree queries. Predicate returns true when matched
		// (the octree's "stop" signal). Inlines per call site, so each query method stays branch-free.
		template <typename PredicateFn>
		bool FindFirstMatch(const FBoxCenterAndExtent& QueryBounds, PredicateFn&& Predicate) const
		{
			if (!Octree)
			{
				return false;
			}
			bool bFound = false;
			Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
			{
				if (Predicate(Item))
				{
					bFound = true;
					return false;
				}
				return true;
			});
			return bFound;
		}

	public:
		FCollection() = default;
		virtual ~FCollection() = default;

		// Building

		virtual void Reserve(int32 Count);

		virtual void Add(const FOBB& Box);

		void Add(const FTransform& Transform, const FBox& LocalBox, int32 Index = -1);

		virtual void BuildOctree();

		virtual void Reset();

		void BuildFrom(const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExPointBoundsSource BoundsSource);

		FORCEINLINE int32 Num() const
		{
			return Bounds.Num();
		}

		FORCEINLINE bool IsEmpty() const
		{
			return Bounds.IsEmpty();
		}

		FORCEINLINE const FBounds& GetBounds(const int32 Index) const
		{
			return Bounds[Index];
		}

		FORCEINLINE const FOrientation& GetOrientation(const int32 Index) const
		{
			return Orientations[Index];
		}

		FORCEINLINE FOBB GetOBB(const int32 Index) const
		{
			return FOBB(Bounds[Index], Orientations[Index]);
		}

		FORCEINLINE const FBox& GetWorldBounds() const
		{
			return WorldBounds;
		}

		FORCEINLINE PCGExOctree::FItemOctree* GetOctree() const
		{
			return Octree.Get();
		}

		// Raw array access for advanced use
		FORCEINLINE const TArray<FBounds>& GetBoundsArray() const
		{
			return Bounds;
		}

		FORCEINLINE const TArray<FOrientation>& GetOrientationsArray() const
		{
			return Orientations;
		}

		// Point queries

		/** Test if point is inside any OBB (runtime mode) */
		bool IsPointInside(const FVector& Point, const EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, const float Expansion = 0.0f) const;

		/** Test if point is inside any OBB, return which one */
		bool IsPointInside(const FVector& Point, int32& OutIndex, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Template version for compile-time mode */
		template <typename Policy>
		bool IsPointInside(const FVector& Point, Policy TestPolicy = Policy{}) const
		{
			if (!Octree)
			{
				return false;
			}

			const float Exp = TestPolicy.Expansion;
			const FBoxCenterAndExtent QueryBounds(Point, FVector4(Exp, Exp, Exp, Exp));

			bool bFound = false;
			Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
			{
				if (TestPolicy.TestPoint(GetOBB(Item.Index), Point))
				{
					bFound = true;
					return false;
				}
				return true;
			});

			return bFound;
		}

		/** Find all OBBs containing a point */
		void FindContaining(const FVector& Point, TArray<int32>& OutIndices, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		// OBB-OBB queries

		/** Test if query OBB overlaps any in collection */
		bool Overlaps(const FOBB& Query, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Test if query OBB is fully contained inside any OBB in collection */
		bool Contains(const FOBB& Query, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Test if query OBB is fully contained in OR overlaps any OBB in collection. Single octree traversal. */
		bool ContainsOrOverlaps(const FOBB& Query, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Template version for compile-time mode */
		template <typename Policy>
		bool Overlaps(const FOBB& Query, Policy TestPolicy = Policy{}) const
		{
			if (!Octree)
			{
				return false;
			}

			const float R = Query.Bounds.Radius + TestPolicy.Expansion;
			const FBoxCenterAndExtent QueryBounds(Query.Bounds.Origin, FVector4(R, R, R, R));

			bool bFound = false;
			Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
			{
				if (TestPolicy.TestOverlap(GetOBB(Item.Index), Query))
				{
					bFound = true;
					return false;
				}
				return true;
			});

			return bFound;
		}

		/** Find first overlapping OBB */
		bool FindFirstOverlap(const FOBB& Query, int32& OutIndex, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Find all overlapping OBBs */
		void FindAllOverlaps(const FOBB& Query, TArray<int32>& OutIndices, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Iterate with callback */
		template <typename Callback>
		void ForEachOverlap(const FOBB& Query, Callback&& Func, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const
		{
			if (!Octree)
			{
				return;
			}

			const float R = Query.Bounds.Radius + Expansion;
			const FBoxCenterAndExtent QueryBounds(Query.Bounds.Origin, FVector4(R, R, R, R));

			Octree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
			{
				if (TestOverlap(GetOBB(Item.Index), Query, Mode, Expansion))
				{
					Func(GetOBB(Item.Index), Item.Index);
				}
			});
		}

		// Filtered OBB-OBB queries (virtual so FDynamicCollection can override with its two-tier approach)

		/** Check overlap with SkipIndex. */
		virtual bool OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex = INDEX_NONE) const;

		/** Check overlap with SkipIndex and per-entry callback filter. Callback receives stored item index (OBB.Bounds.Index), returns true to SKIP. */
		virtual bool OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex, TFunctionRef<bool(int32)> ShouldSkip) const;

		/** Check if any entry overlaps with penetration depth exceeding threshold. */
		virtual bool OverlapsBeyondThreshold(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex = INDEX_NONE) const;

		/** Check threshold-overlap with SkipIndex and per-entry callback filter. Callback receives stored item index (OBB.Bounds.Index), returns true to SKIP. */
		virtual bool OverlapsBeyondThreshold(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex, TFunctionRef<bool(int32)> ShouldSkip) const;

		/** SAT-culls collection and calls ConfirmOverlap for each survivor. Returns true on first ConfirmOverlap that returns true. */
		virtual bool ForEachOverlapping(const FOBB& Candidate, int32 SkipIndex, TFunctionRef<bool(int32)> ShouldSkipOwner, TFunctionRef<bool(const FOBB&, int32 OwnerIndex)> ConfirmOverlap) const;

		// Intersection queries

		/** Find all segment intersections */
		bool FindIntersections(FIntersections& IO) const;

		/** Quick test if segment intersects any OBB */
		bool SegmentIntersectsAny(const FVector& Start, const FVector& End) const;

		// Bulk operations

		/** Classify points as inside/outside */
		void ClassifyPoints(TArrayView<const FVector> Points, TBitArray<>& OutInside, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		/** Filter to points inside any OBB */
		void FilterInside(TArrayView<const FVector> Points, TArray<int32>& OutIndices, EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box, float Expansion = 0.0f) const;

		// Bounds queries

		bool LooseOverlaps(const FBox& Box) const
		{
			return WorldBounds.Intersect(Box);
		}

		bool Overlaps(const FBox& Box) const
		{
			if (!WorldBounds.Intersect(Box) && !WorldBounds.IsInside(Box))
			{
				return false;
			}
			return Overlaps(Factory::FromAABB(Box, -1));
		}

		bool Encompasses(const FBox& Box) const
		{
			if (!WorldBounds.Intersect(Box) && !WorldBounds.IsInside(Box))
			{
				return false;
			}

			// All 8 corners must be inside
			const FVector Corners[8] = {
				{Box.Min.X, Box.Min.Y, Box.Min.Z}, {Box.Max.X, Box.Min.Y, Box.Min.Z},
				{Box.Min.X, Box.Max.Y, Box.Min.Z}, {Box.Max.X, Box.Max.Y, Box.Min.Z},
				{Box.Min.X, Box.Min.Y, Box.Max.Z}, {Box.Max.X, Box.Min.Y, Box.Max.Z},
				{Box.Min.X, Box.Max.Y, Box.Max.Z}, {Box.Max.X, Box.Max.Y, Box.Max.Z},
			};

			for (int32 i = 0; i < 8; i++)
			{
				if (!IsPointInside(Corners[i]))
				{
					return false;
				}
			}
			return true;
		}
	};

	/**
	 * Dynamic OBB collection with incremental insertion and periodic octree rebuilds.
	 * Designed for sequential growth (modules placed one at a time) with fast overlap queries.
	 * Uses a two-tier approach: octree for bulk entries + linear scan for pending entries.
	 * Extends FCollection to reuse its SoA data layout and octree infrastructure.
	 */
	class PCGEXCORE_API FDynamicCollection : public FCollection
	{
	public:
		FDynamicCollection() = default;
		virtual ~FDynamicCollection() override = default;

		/** Insert a new OBB. Expands world bounds and triggers periodic octree rebuild. Returns entry index. */
		virtual void Add(const FOBB& Box) override;

		/** Also pre-sizes the validity bits the base has no knowledge of. */
		virtual void Reserve(int32 Count) override;

		/** Rebuild octree from all valid (non-invalidated) entries. */
		virtual void BuildOctree() override;

		/** Reset all data including dynamic state. */
		virtual void Reset() override;

		/** Returns the index of the last added entry. */
		FORCEINLINE int32 LastIndex() const
		{
			return Num() - 1;
		}

		/** Mark entries from FromIndex onward as invalid (for grammar backtracking). */
		void Invalidate(int32 FromIndex);

		/** Two-tier overlap check (octree + pending) with ValidMask filtering. */
		virtual bool OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex = INDEX_NONE) const override;

		/** Two-tier overlap check with ValidMask + per-entry callback filter. Callback receives stored item index (OBB.Bounds.Index), returns true to SKIP. */
		virtual bool OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex, TFunctionRef<bool(int32)> ShouldSkip) const override;

		/** Two-tier threshold-overlap check (octree + pending) with ValidMask filtering. */
		virtual bool OverlapsBeyondThreshold(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex = INDEX_NONE) const override;

		/** Two-tier threshold-overlap check with ValidMask + per-entry callback filter. Callback receives stored item index (OBB.Bounds.Index), returns true to SKIP. */
		virtual bool OverlapsBeyondThreshold(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex,
		                                     TFunctionRef<bool(int32)> ShouldSkip) const override;

		/** Two-tier SAT enumerator (octree + pending) with ValidMask filtering; calls ConfirmOverlap for each survivor. */
		virtual bool ForEachOverlapping(
			const FOBB& Candidate,
			int32 SkipIndex,
			TFunctionRef<bool(int32)> ShouldSkipOwner,
			TFunctionRef<bool(const FOBB&, int32 OwnerIndex)> ConfirmOverlap) const override;

		/** Count valid (non-invalidated) entries. */
		int32 NumValid() const;

		/** How many pending inserts before the octree is brought up to date. */
		int32 RebuildInterval = 32;

	private:
		TBitArray<> ValidMask;

		/** Entries [0..OctreeCount-1] are in the octree; [OctreeCount..Num()-1] are pending. */
		int32 OctreeCount = 0;

		/**
		 * World box the current octree was built to span. Pending entries that fit inside
		 * it are inserted incrementally; anything outside needs a re-centered octree.
		 */
		FBox OctreeCoverage = FBox(ForceInit);

		/**
		 * How many items were actually inserted into the current octree. NOT OctreeCount,
		 * which is an index boundary and counts entries that were skipped as invalid --
		 * using it as the purge denominator understates staleness.
		 */
		int32 OctreeInsertedCount = 0;

		/**
		 * How many of OctreeInsertedCount have since been masked out by Invalidate. They keep
		 * costing traversal until a rebuild drops them, so this drives the purge threshold.
		 */
		int32 OctreeStaleCount = 0;

		/** Bring the octree up to date -- incrementally when possible, else a full rebuild. */
		void MaybeRebuildOctree();

		/**
		 * Insert the pending tail into the existing octree. Fails (mutating nothing) when
		 * any pending entry falls outside OctreeCoverage, leaving a rebuild as the caller's
		 * only option.
		 */
		bool TryFlushPendingIncremental();

		/** Templated overlap implementation for code reuse. */
		template <typename FilterFn>
		bool OverlapsImpl(const FOBB& Candidate, int32 SkipIndex, FilterFn&& Filter) const;

		/** Templated penetration-threshold overlap implementation for code reuse. */
		template <typename FilterFn>
		bool OverlapsBeyondThresholdImpl(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex, FilterFn&& Filter) const;

		/** Two-tier traversal: Filter gates each entry, OnMatch(StoredOBB, EntryIndex) stops on true. Caches GetOBB per SAT-passing candidate. */
		template <typename FilterFn, typename MatchFn>
		bool ForEachImpl(const FOBB& Candidate, int32 SkipIndex, FilterFn&& Filter, MatchFn&& OnMatch) const;
	};
}
