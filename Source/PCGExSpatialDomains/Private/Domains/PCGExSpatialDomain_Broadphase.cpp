// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Domains/PCGExSpatialDomain_Broadphase.h"

#include "Math/OBB/PCGExOBB.h"
#include "NarrowPhase/PCGExNarrowPhase.h"
#include "Settings/PCGExSpatialDomainsSettings.h"
#include "Shapes/PCGExFootprintShape.h"

FPCGExSpatialDomain_Broadphase::FPCGExSpatialDomain_Broadphase()
	: MatrixRef(&UPCGExSpatialDomainsSettings::GetCompiledMatrix())
{
}

float FPCGExSpatialDomain_Broadphase::QueryPoint(const FVector& Point) const
{
	// Union SDF: walk valid entries, dispatch per-shape signed distance via
	// the narrow-phase QueryPoint registry, combine via min (CSG-union for
	// "inside any entry"). Entries whose kind has no registered QueryPoint
	// fn contribute +INFINITY -- safely ignored by the min.
	//
	// No broadphase pruning: the SDF magnitude across an entry's AABB is
	// only loosely bounded by AABB distance, so an entry far from Point's
	// AABB could still win the min if other entries are farther. A future
	// optimization could maintain a "best so far" and skip entries whose
	// AABB-to-point distance exceeds it -- but for the external-constraint
	// use case (single-digit entries per channel typically) the walk is
	// already cheap. Add the pruner if profiling demands.
	if (NumValidEntries == 0)
	{
		return TNumericLimits<float>::Max();
	}

	float Best = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (!ValidMask[i])
		{
			continue;
		}
		const FEntry& E = Entries[i];
		if (E.KindTag == PCGExSpatial::NarrowPhase::InvalidKindTag)
		{
			continue;
		}

		const FPCGExFootprintShape* ShapePtr =
			reinterpret_cast<const FPCGExFootprintShape*>(E.Shape.GetMemory());
		if (!ShapePtr)
		{
			continue;
		}

		const float D = PCGExSpatial::NarrowPhase::QueryPoint(E.KindTag, Point, *ShapePtr);
		Best = FMath::Min(Best, D);
	}
	return Best;
}

bool FPCGExSpatialDomain_Broadphase::Overlaps(
	const FPCGExFootprintShape& Candidate,
	int32 SkipOwnerIndex,
	TFunctionRef<bool(int32)> ShouldSkip,
	uint32 CandidateChannelMask) const
{
	check(MatrixRef);
	if (NumValidEntries == 0)
	{
		return false;
	}

	const FBox CandidateAABB = Candidate.GetWorldAABB();
	if (!CandidateAABB.IsValid)
	{
		return false;
	}

	// Resolve the candidate tag once. Unregistered kinds have no pair tests
	// in any direction -- early-out matches the "no impl => no overlap" safe
	// default (same as the slow-path TestOverlap wrapper).
	const PCGExSpatial::NarrowPhase::FShapeKindTag CandidateTag =
		PCGExSpatial::NarrowPhase::FindShapeKindTag(Candidate.GetScriptStruct());
	if (CandidateTag == PCGExSpatial::NarrowPhase::InvalidKindTag)
	{
		return false;
	}

	// Candidate enters the broadphase as an identity-orientation OBB so
	// the collection's SAT path is AABB-vs-AABB on both sides -- cheap,
	// tight, and the "real" overlap question is answered by the narrow
	// phase per surviving entry.
	const PCGExMath::OBB::FOBB CandidateOBB = PCGExMath::OBB::Factory::FromAABB(CandidateAABB, INDEX_NONE);

	// ShouldSkipOwner predicate runs per broadphase-culled entry. The
	// FOBB's Bounds.Index is our STORAGE index (set in Append); resolve
	// owner index via Entries[].OwnerIndex.
	auto SkipPredicate = [this, SkipOwnerIndex, &ShouldSkip](int32 StorageIdx) -> bool
	{
		if (!ValidMask.IsValidIndex(StorageIdx) || !ValidMask[StorageIdx])
		{
			return true;
		}
		const int32 OwnerIdx = Entries[StorageIdx].OwnerIndex;
		return (SkipOwnerIndex != INDEX_NONE && OwnerIdx == SkipOwnerIndex)
			|| ShouldSkip(OwnerIdx);
	};

	// ConfirmOverlap runs per SAT-confirmed (AABB-confirmed) entry. Two
	// gates before the narrow phase:
	//   1. Channel matrix -- skip pairs whose (candidate, stored) response
	//      is Ignore. Cheap table read; saves us a registry dispatch on
	//      author-declared "don't care" pairs.
	//   2. Narrow phase via the registry on survivors.
	auto ConfirmOverlap = [this, &Candidate, CandidateTag, CandidateChannelMask](
		const PCGExMath::OBB::FOBB&, int32 StorageIdx) -> bool
	{
		const FEntry& Entry = Entries[StorageIdx];
		if (!MatrixRef->ShouldRunNarrowPhase(CandidateChannelMask, Entry.ChannelMask))
		{
			return false;
		}

		const FPCGExFootprintShape* StoredShape =
			reinterpret_cast<const FPCGExFootprintShape*>(Entry.Shape.GetMemory());
		return PCGExSpatial::NarrowPhase::TestOverlap(
			CandidateTag, Candidate, Entry.KindTag, *StoredShape);
	};

	return BroadphaseAABBs.ForEachOverlapping(CandidateOBB, INDEX_NONE,
	                                          SkipPredicate, ConfirmOverlap);
}

bool FPCGExSpatialDomain_Broadphase::OverlapsBeyondThreshold(
	const FPCGExFootprintShape& Candidate,
	float MaxAllowedPenetration,
	int32 SkipOwnerIndex,
	uint32 CandidateChannelMask) const
{
	check(MatrixRef);
	if (NumValidEntries == 0)
	{
		return false;
	}

	const FBox CandidateAABB = Candidate.GetWorldAABB();
	if (!CandidateAABB.IsValid)
	{
		return false;
	}

	const PCGExSpatial::NarrowPhase::FShapeKindTag CandidateTag =
		PCGExSpatial::NarrowPhase::FindShapeKindTag(Candidate.GetScriptStruct());
	if (CandidateTag == PCGExSpatial::NarrowPhase::InvalidKindTag)
	{
		return false;
	}

	const PCGExMath::OBB::FOBB CandidateOBB = PCGExMath::OBB::Factory::FromAABB(CandidateAABB, INDEX_NONE);

	auto SkipPredicate = [this, SkipOwnerIndex](int32 StorageIdx) -> bool
	{
		if (!ValidMask.IsValidIndex(StorageIdx) || !ValidMask[StorageIdx])
		{
			return true;
		}
		const int32 OwnerIdx = Entries[StorageIdx].OwnerIndex;
		return SkipOwnerIndex != INDEX_NONE && OwnerIdx == SkipOwnerIndex;
	};

	// Per-entry penetration test via the registry. Channel matrix gate
	// runs first -- Ignored pairs skip without invoking the registry's
	// QueryPenetration dispatch. First survivor whose magnitude exceeds
	// the threshold rejects the candidate.
	auto ConfirmExceedsThreshold = [this, &Candidate, CandidateTag, MaxAllowedPenetration, CandidateChannelMask](
		const PCGExMath::OBB::FOBB&, int32 StorageIdx) -> bool
	{
		const FEntry& Entry = Entries[StorageIdx];
		if (!MatrixRef->ShouldRunNarrowPhase(CandidateChannelMask, Entry.ChannelMask))
		{
			return false;
		}

		const FPCGExFootprintShape* StoredShape =
			reinterpret_cast<const FPCGExFootprintShape*>(Entry.Shape.GetMemory());
		const float Pen = PCGExSpatial::NarrowPhase::QueryPenetration(
			CandidateTag, Candidate, Entry.KindTag, *StoredShape);
		return Pen > MaxAllowedPenetration;
	};

	return BroadphaseAABBs.ForEachOverlapping(CandidateOBB, INDEX_NONE,
	                                          SkipPredicate, ConfirmExceedsThreshold);
}

int32 FPCGExSpatialDomain_Broadphase::Append(
	const FPCGExFootprintShape& Shape,
	int32 OwnerIndex,
	uint32 ChannelMask)
{
	// Owner contract: a real placed index (>= 0) or the explicit ObstacleOwner sentinel.
	// INDEX_NONE is the skip-nothing sentinel and would mark an accidental unowned entry.
	check(OwnerIndex >= 0 || OwnerIndex == FPCGExSpatialDomain::ObstacleOwner);

	UScriptStruct* StructType = Shape.GetScriptStruct();
	if (!StructType)
	{
		return INDEX_NONE;
	}

	const FBox WorldAABB = Shape.GetWorldAABB();
	if (!WorldAABB.IsValid)
	{
		return INDEX_NONE;
	}

	// Auto-tag the kind so future Appends with the same struct hit the
	// cache. Pair tests register tags too, so by the time Append runs
	// the tag usually already exists; this guards storage backends that
	// pre-populate before pair-test modules have loaded.
	const PCGExSpatial::NarrowPhase::FShapeKindTag KindTag =
		PCGExSpatial::NarrowPhase::RegisterShapeKind(StructType);

	const int32 StorageIdx = Entries.Num();

	FEntry& Entry = Entries.AddDefaulted_GetRef();
	// Dynamic-typed copy: reads the UScriptStruct's CppStructOps to clone
	// the runtime shape's memory into the instanced-struct wrapper.
	Entry.Shape.InitializeAs(StructType, reinterpret_cast<const uint8*>(&Shape));
	Entry.OwnerIndex = OwnerIndex;
	Entry.WorldAABB = WorldAABB;
	Entry.KindTag = KindTag;
	Entry.ChannelMask = ChannelMask;

	ValidMask.Add(true);
	++NumValidEntries;
	WorldBounds += WorldAABB;
	EntriesByOwner.FindOrAdd(OwnerIndex).Add(StorageIdx);

	// Lens AABB as identity-orientation OBB; Bounds.Index = StorageIdx so
	// the broadphase walk's per-entry callbacks can recover the entry.
	BroadphaseAABBs.Add(PCGExMath::OBB::Factory::FromAABB(WorldAABB, StorageIdx));

	return StorageIdx;
}

void FPCGExSpatialDomain_Broadphase::Reserve(int32 ExpectedCount)
{
	Entries.Reserve(ExpectedCount);
	ValidMask.Reserve(ExpectedCount);
	BroadphaseAABBs.Reserve(ExpectedCount);
}

void FPCGExSpatialDomain_Broadphase::BeginBulkAppend()
{
	if (BulkSavedRebuildInterval != INDEX_NONE)
	{
		return; // already scoped -- keep the original cadence to restore
	}

	// A rebuild cadence no pending-count can reach makes every Append in the scope
	// storage-only. Without this, an N-append batch rebuilds the octree N/32 times,
	// each walking every entry so far -- O(N^2) insertion for a one-shot load.
	BulkSavedRebuildInterval = BroadphaseAABBs.RebuildInterval;
	BulkEntryCountAtBegin = Entries.Num();
	BroadphaseAABBs.RebuildInterval = MAX_int32;
}

void FPCGExSpatialDomain_Broadphase::EndBulkAppend()
{
	if (BulkSavedRebuildInterval == INDEX_NONE)
	{
		return; // no matching Begin
	}

	BroadphaseAABBs.RebuildInterval = BulkSavedRebuildInterval;
	BulkSavedRebuildInterval = INDEX_NONE;

	// An empty scope leaves the accelerator exactly as it was found -- rebuilding here
	// would discard a good octree and re-add every entry for nothing.
	if (Entries.Num() == BulkEntryCountAtBegin)
	{
		return;
	}

	// Everything appended in the scope is still pending (and therefore linear-scanned
	// by queries) until this lands.
	BroadphaseAABBs.BuildOctree();
}

FPCGExSpatialDomain::FSnapshotHandle FPCGExSpatialDomain_Broadphase::BeginSnapshotScope()
{
	// High-water mark; rollback flips ValidMask bits past the handle.
	// O(1) amortized, no realloc.
	return Entries.Num();
}

void FPCGExSpatialDomain_Broadphase::RollbackToScope(FSnapshotHandle Handle)
{
	const int32 RollbackTo = Handle;
	if (RollbackTo < 0 || RollbackTo >= Entries.Num())
	{
		return;
	}

	for (int32 i = RollbackTo; i < Entries.Num(); ++i)
	{
		if (ValidMask[i])
		{
			ValidMask[i] = false;
			--NumValidEntries;
		}
	}

	// Roll the broadphase backing in the same direction. FDynamicCollection's
	// Invalidate flips its own ValidMask bits past FromIndex -- same O(1)
	// amortized model. Storage indices are stable across rollback (entries
	// stay in memory; only the validity bit flips), so the FOBB's
	// Bounds.Index inside BroadphaseAABBs still maps to Entries[].
	BroadphaseAABBs.Invalidate(RollbackTo);

	// Bounds management:
	//   - Full rollback (no entries valid): reset to empty so the next seed
	//     gets a tight, honest bound rather than ghost extents.
	//   - Partial rollback: leave WorldBounds alone (over-approximation).
	//     The only consumer is a pre-cull (Domain.GetBounds().Intersect(...)),
	//     which is permissive-direction safe -- the per-entry walk through
	//     BroadphaseAABBs still rejects invalid entries via ValidMask.
	//     Recomputing here would be O(N) and break the advertised O(1)
	//     amortized rollback model that the grammar speculation loop relies on.
	if (NumValidEntries == 0)
	{
		WorldBounds = FBox(ForceInit);
	}
}

int32 FPCGExSpatialDomain_Broadphase::InvalidateOwner(const int32 OwnerIndex)
{
	// Append rejects INDEX_NONE as an owner, so matching it could only ever be a caller bug.
	if (OwnerIndex == INDEX_NONE)
	{
		return 0;
	}

	const TArray<int32>* Owned = EntriesByOwner.Find(OwnerIndex);
	if (!Owned)
	{
		return 0;
	}

	int32 Flipped = 0;
	for (const int32 StorageIdx : *Owned)
	{
		if (ValidMask[StorageIdx])
		{
			ValidMask[StorageIdx] = false;
			--NumValidEntries;
			++Flipped;
		}
	}

	// WorldBounds is deliberately not shrunk -- same over-approximation contract as a partial
	// RollbackToScope, and its only consumer is a permissive-direction pre-cull.
	if (NumValidEntries == 0)
	{
		WorldBounds = FBox(ForceInit);
	}

	return Flipped;
}

int32 FPCGExSpatialDomain_Broadphase::RevalidateOwner(const int32 OwnerIndex)
{
	if (OwnerIndex == INDEX_NONE)
	{
		return 0;
	}

	const TArray<int32>* Owned = EntriesByOwner.Find(OwnerIndex);
	if (!Owned)
	{
		return 0;
	}

	int32 Flipped = 0;
	for (const int32 StorageIdx : *Owned)
	{
		if (!ValidMask[StorageIdx])
		{
			ValidMask[StorageIdx] = true;
			++NumValidEntries;
			++Flipped;
			WorldBounds += Entries[StorageIdx].WorldAABB;
		}
	}

	return Flipped;
}
