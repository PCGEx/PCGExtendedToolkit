// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/OBB/PCGExOBBCollection.h"

#include "Data/PCGExPointIO.h"

namespace PCGExMath::OBB
{
	void FCollection::Reserve(int32 Count)
	{
		Bounds.Reserve(Count);
		Orientations.Reserve(Count);
	}

	void FCollection::Add(const FOBB& Box)
	{
		Bounds.Add(Box.Bounds);
		Orientations.Add(Box.Orientation);
	}

	void FCollection::Add(const FTransform& Transform, const FBox& LocalBox, int32 Index)
	{
		WorldBounds += LocalBox.TransformBy(Transform.ToMatrixNoScale());
		Add(Factory::FromTransform(Transform, LocalBox, Index >= 0 ? Index : Bounds.Num()));
	}

	void FCollection::BuildOctree()
	{
		if (Bounds.IsEmpty())
		{
			Octree.Reset();
			return;
		}

		const FVector Extent = WorldBounds.GetExtent();
		const float MaxExtent = FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 1.5f;

		Octree = MakeUnique<PCGExOctree::FItemOctree>(WorldBounds.GetCenter(), MaxExtent);

		const int32 Count = Bounds.Num();
		for (int32 i = 0; i < Count; i++)
		{
			const FBounds& B = Bounds[i];
			Octree->AddElement(PCGExOctree::FItem(i, FBoxSphereBounds(B.Origin, FVector(B.Radius), B.Radius)));
		}
	}

	void FCollection::Reset()
	{
		Bounds.Reset();
		Orientations.Reset();
		Octree.Reset();
		WorldBounds = FBox(ForceInit);
	}

	void FCollection::BuildFrom(const TSharedPtr<PCGExData::FPointIO>& InIO, const EPCGExPointBoundsSource BoundsSource)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExMath::OBB::FCollection::BuildFrom);

		const int32 NumPoints = InIO->GetNum();
		Reserve(NumPoints);

		for (int32 i = 0; i < NumPoints; i++)
		{
			const PCGExData::FConstPoint Point = InIO->GetInPoint(i);
			Add(Point.GetTransform(), GetLocalBounds(Point, BoundsSource), i);
		}

		BuildOctree();
	}

	bool FCollection::IsPointInside(const FVector& Point, const EPCGExBoxCheckMode Mode, const float Expansion) const
	{
		if (!Octree)
		{
			return false;
		}

		const FBoxCenterAndExtent QueryBounds(Point, FVector4(Expansion, Expansion, Expansion, Expansion));

		bool bFound = false;
		Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
		{
			if (TestPoint(GetOBB(Item.Index), Point, Mode, Expansion))
			{
				bFound = true;
				return false;
			}
			return true;
		});

		return bFound;
	}

	bool FCollection::IsPointInside(const FVector& Point, int32& OutIndex, EPCGExBoxCheckMode Mode, float Expansion) const
	{
		if (!Octree)
		{
			return false;
		}

		const FBoxCenterAndExtent QueryBounds(Point, FVector4(Expansion, Expansion, Expansion, Expansion));

		bool bFound = false;
		Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
		{
			if (TestPoint(GetOBB(Item.Index), Point, Mode, Expansion))
			{
				OutIndex = Bounds[Item.Index].Index;
				bFound = true;
				return false;
			}
			return true;
		});

		return bFound;
	}

	void FCollection::FindContaining(const FVector& Point, TArray<int32>& OutIndices, EPCGExBoxCheckMode Mode, float Expansion) const
	{
		if (!Octree)
		{
			return;
		}

		const FBoxCenterAndExtent QueryBounds(Point, FVector4(Expansion, Expansion, Expansion, Expansion));

		Octree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
		{
			if (TestPoint(GetOBB(Item.Index), Point, Mode, Expansion))
			{
				OutIndices.Add(Bounds[Item.Index].Index);
			}
		});
	}

	bool FCollection::Overlaps(const FOBB& Query, EPCGExBoxCheckMode Mode, float Expansion) const
	{
		if (!Octree)
		{
			return false;
		}

		const float R = Query.Bounds.Radius + Expansion;
		const FBoxCenterAndExtent QueryBounds(Query.Bounds.Origin, FVector4(R, R, R, R));

		bool bFound = false;
		Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
		{
			if (TestOverlap(GetOBB(Item.Index), Query, Mode, Expansion))
			{
				bFound = true;
				return false;
			}
			return true;
		});

		return bFound;
	}

	bool FCollection::FindFirstOverlap(const FOBB& Query, int32& OutIndex, EPCGExBoxCheckMode Mode, float Expansion) const
	{
		if (!Octree)
		{
			return false;
		}

		const float R = Query.Bounds.Radius + Expansion;
		const FBoxCenterAndExtent QueryBounds(Query.Bounds.Origin, FVector4(R, R, R, R));

		bool bFound = false;
		Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
		{
			if (TestOverlap(GetOBB(Item.Index), Query, Mode, Expansion))
			{
				OutIndex = Bounds[Item.Index].Index;
				bFound = true;
				return false;
			}
			return true;
		});

		return bFound;
	}

	void FCollection::FindAllOverlaps(const FOBB& Query, TArray<int32>& OutIndices, EPCGExBoxCheckMode Mode, float Expansion) const
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
				OutIndices.Add(Bounds[Item.Index].Index);
			}
		});
	}

	bool FCollection::FindIntersections(FIntersections& IO) const
	{
		if (!Octree)
		{
			return false;
		}

		const FBoxCenterAndExtent QueryBounds = IO.GetBounds();

		Octree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item)
		{
			ProcessSegment(GetOBB(Item.Index), IO, CloudIndex);
		});

		return !IO.IsEmpty();
	}

	bool FCollection::SegmentIntersectsAny(const FVector& Start, const FVector& End) const
	{
		if (!Octree)
		{
			return false;
		}

		FBox SegBox(ForceInit);
		SegBox += Start;
		SegBox += End;
		const FBoxCenterAndExtent QueryBounds(SegBox);

		bool bFound = false;
		Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
		{
			if (SegmentIntersects(GetOBB(Item.Index), Start, End))
			{
				bFound = true;
				return false;
			}
			return true;
		});

		return bFound;
	}

	void FCollection::ClassifyPoints(TArrayView<const FVector> Points, TBitArray<>& OutInside, EPCGExBoxCheckMode Mode, float Expansion) const
	{
		const int32 N = Points.Num();
		OutInside.Init(false, N);

		for (int32 i = 0; i < N; i++)
		{
			OutInside[i] = IsPointInside(Points[i], Mode, Expansion);
		}
	}

	void FCollection::FilterInside(TArrayView<const FVector> Points, TArray<int32>& OutIndices, EPCGExBoxCheckMode Mode, float Expansion) const
	{
		const int32 N = Points.Num();
		OutIndices.Reserve(N / 4);

		for (int32 i = 0; i < N; i++)
		{
			if (IsPointInside(Points[i], Mode, Expansion))
			{
				OutIndices.Add(i);
			}
		}
	}

	// ========== FDynamicCollection ==========

#pragma region FDynamicCollection

	void FDynamicCollection::Add(const FOBB& Box)
	{
		// Expand world bounds before adding to arrays
		const FVector& Origin = Box.Bounds.Origin;
		const float Radius = Box.Bounds.Radius;
		const FBox EntryBox(Origin - FVector(Radius), Origin + FVector(Radius));

		if (Bounds.IsEmpty())
		{
			WorldBounds = EntryBox;
		}
		else
		{
			WorldBounds += EntryBox;
		}

		// Add to inherited SoA arrays
		FCollection::Add(Box);

		ValidMask.Add(true);
		MaybeRebuildOctree();
	}

	void FDynamicCollection::BuildOctree()
	{
		if (Bounds.IsEmpty())
		{
			Octree.Reset();
			OctreeCount = 0;
			return;
		}

		const FVector Center = WorldBounds.GetCenter();
		const FVector Extent = WorldBounds.GetExtent();
		const float MaxExtent = FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 1.5f;

		Octree = MakeUnique<PCGExOctree::FItemOctree>(Center, MaxExtent);

		const int32 Count = Bounds.Num();
		for (int32 i = 0; i < Count; ++i)
		{
			if (!ValidMask[i]) { continue; }

			const FBounds& B = Bounds[i];
			Octree->AddElement(PCGExOctree::FItem(
				i,
				FBoxSphereBounds(B.Origin, FVector(B.Radius), B.Radius)));
		}

		OctreeCount = Count;
	}

	void FDynamicCollection::Reset()
	{
		FCollection::Reset();
		ValidMask.Empty();
		OctreeCount = 0;
	}

	void FDynamicCollection::Invalidate(int32 FromIndex)
	{
		for (int32 i = FromIndex; i < ValidMask.Num(); ++i)
		{
			ValidMask[i] = false;
		}
	}

	int32 FDynamicCollection::NumValid() const
	{
		int32 Count = 0;
		for (int32 i = 0; i < ValidMask.Num(); ++i)
		{
			if (ValidMask[i]) { Count++; }
		}
		return Count;
	}

	void FDynamicCollection::MaybeRebuildOctree()
	{
		const int32 PendingCount = Num() - OctreeCount;
		if (PendingCount >= RebuildInterval)
		{
			BuildOctree();
		}
	}

	template<typename FilterFn>
	bool FDynamicCollection::OverlapsImpl(const FOBB& Candidate, int32 SkipIndex, FilterFn&& Filter) const
	{
		// 1. Octree query (entries 0..OctreeCount-1)
		if (Octree && OctreeCount > 0)
		{
			const float R = Candidate.Bounds.Radius;
			const FBoxCenterAndExtent QueryBounds(Candidate.Bounds.Origin, FVector4(R, R, R, R));

			bool bFound = false;
			Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
			{
				const int32 i = Item.Index;
				if (i == SkipIndex) { return true; } // Continue
				if (ValidMask.IsValidIndex(i) && !ValidMask[i]) { return true; } // Skip invalid
				if (!Filter(i)) { return true; } // Custom filter says skip

				if (SphereOverlap(GetBounds(i), Candidate.Bounds)
					&& SATOverlap(GetOBB(i), Candidate))
				{
					bFound = true;
					return false; // Stop
				}
				return true; // Continue
			});

			if (bFound) { return true; }
		}

		// 2. Linear scan pending entries (OctreeCount..Num()-1)
		const int32 Total = Num();
		for (int32 i = OctreeCount; i < Total; ++i)
		{
			if (i == SkipIndex) { continue; }
			if (ValidMask.IsValidIndex(i) && !ValidMask[i]) { continue; }
			if (!Filter(i)) { continue; }

			if (SphereOverlap(GetBounds(i), Candidate.Bounds)
				&& SATOverlap(GetOBB(i), Candidate))
			{
				return true;
			}
		}

		return false;
	}

	bool FDynamicCollection::OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex) const
	{
		return OverlapsImpl(Candidate, SkipIndex, [](int32) { return true; });
	}

	bool FDynamicCollection::OverlapsFiltered(const FOBB& Candidate, int32 SkipIndex, TFunctionRef<bool(int32)> ShouldSkip) const
	{
		// ShouldSkip receives the stored module index (Bounds.Index), not the entry index.
		// Filter returns true to KEEP, false to SKIP — inverted from ShouldSkip.
		return OverlapsImpl(Candidate, SkipIndex, [this, &ShouldSkip](int32 i) { return !ShouldSkip(GetBounds(i).Index); });
	}

	bool FDynamicCollection::OverlapsBeyondThreshold(const FOBB& Candidate, float MaxPenetration, int32 SkipIndex) const
	{
		// Octree query for entries in the octree
		if (Octree && OctreeCount > 0)
		{
			const float R = Candidate.Bounds.Radius;
			const FBoxCenterAndExtent QueryBounds(Candidate.Bounds.Origin, FVector4(R, R, R, R));

			bool bFound = false;
			Octree->FindFirstElementWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& Item) -> bool
			{
				const int32 i = Item.Index;
				if (i == SkipIndex) { return true; }
				if (ValidMask.IsValidIndex(i) && !ValidMask[i]) { return true; }

				if (SpherePenetrationDepth(GetBounds(i), Candidate.Bounds) <= 0.0f) { return true; }

				const float Depth = SATPenetrationDepth(GetOBB(i), Candidate);
				if (Depth > MaxPenetration)
				{
					bFound = true;
					return false;
				}
				return true;
			});

			if (bFound) { return true; }
		}

		// Linear scan pending
		const int32 Total = Num();
		for (int32 i = OctreeCount; i < Total; ++i)
		{
			if (i == SkipIndex) { continue; }
			if (ValidMask.IsValidIndex(i) && !ValidMask[i]) { continue; }

			if (SpherePenetrationDepth(GetBounds(i), Candidate.Bounds) <= 0.0f) { continue; }

			const float Depth = SATPenetrationDepth(GetOBB(i), Candidate);
			if (Depth > MaxPenetration)
			{
				return true;
			}
		}

		return false;
	}

#pragma endregion
}
