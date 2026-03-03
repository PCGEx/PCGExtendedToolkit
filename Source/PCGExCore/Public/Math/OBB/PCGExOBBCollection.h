// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "PCGExOBB.h"
#include "PCGExOBBTests.h"
#include "PCGExOBBIntersections.h"
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

	public:
		FCollection() = default;
		virtual ~FCollection() = default;

		// Building

		void Reserve(int32 Count);

		virtual void Add(const FOBB& Box);

		void Add(const FTransform& Transform, const FBox& LocalBox, int32 Index = -1);

		virtual void BuildOctree();

		virtual void Reset();

		void BuildFrom(const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExPointBoundsSource BoundsSource);

		FORCEINLINE int32 Num() const { return Bounds.Num(); }
		FORCEINLINE bool IsEmpty() const { return Bounds.IsEmpty(); }

		FORCEINLINE const FBounds& GetBounds(const int32 Index) const { return Bounds[Index]; }
		FORCEINLINE const FOrientation& GetOrientation(const int32 Index) const { return Orientations[Index]; }

		FORCEINLINE FOBB GetOBB(const int32 Index) const { return FOBB(Bounds[Index], Orientations[Index]); }

		FORCEINLINE const FBox& GetWorldBounds() const { return WorldBounds; }
		FORCEINLINE PCGExOctree::FItemOctree* GetOctree() const { return Octree.Get(); }

		// Raw array access for advanced use
		FORCEINLINE const TArray<FBounds>& GetBoundsArray() const { return Bounds; }
		FORCEINLINE const TArray<FOrientation>& GetOrientationsArray() const { return Orientations; }

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

			for (int32 i = 0; i < 8; i++) { if (!IsPointInside(Corners[i])) { return false; } }
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

		/** Rebuild octree from all valid (non-invalidated) entries. */
		virtual void BuildOctree() override;

		/** Reset all data including dynamic state. */
		virtual void Reset() override;

		/** Returns the index of the last added entry. */
		FORCEINLINE int32 LastIndex() const { return Num() - 1; }

		/** Mark entries from FromIndex onward as invalid (for grammar backtracking). */
		void Invalidate(int32 FromIndex);

		/** Check overlap with optional SkipIndex and ValidMask filtering. */
		bool OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex = INDEX_NONE) const;

		/** Check overlap with SkipIndex and per-entry callback filter. Callback receives the stored item index (OBB.Bounds.Index), returns true to SKIP that entry. */
		bool OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex, TFunctionRef<bool(int32)> ShouldSkip) const;

		/** Check if any entry overlaps with penetration depth exceeding threshold. */
		bool OverlapsBeyondThreshold(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex = INDEX_NONE) const;

		/** Count valid (non-invalidated) entries. */
		int32 NumValid() const;

		/** How many inserts between octree rebuilds. */
		int32 RebuildInterval = 32;

	private:
		TBitArray<> ValidMask;

		/** Entries [0..OctreeCount-1] are in the octree; [OctreeCount..Num()-1] are pending. */
		int32 OctreeCount = 0;

		/** Check if pending count warrants a rebuild. */
		void MaybeRebuildOctree();

		/** Templated overlap implementation for code reuse. */
		template<typename FilterFn>
		bool OverlapsImpl(const FOBB& Candidate, int32 SkipIndex, FilterFn&& Filter) const;
	};
}
