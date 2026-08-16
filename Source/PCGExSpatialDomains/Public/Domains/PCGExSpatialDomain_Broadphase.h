// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Channels/PCGExChannelInteractionMatrix.h"
#include "Domains/PCGExSpatialDomain.h"
#include "Math/OBB/PCGExOBB.h"
#include "Math/OBB/PCGExOBBCollection.h"
#include "NarrowPhase/PCGExNarrowPhase.h"
#include "Shapes/PCGExFootprintShape.h"
#include "StructUtils/InstancedStruct.h"

/**
 * Heterogeneous mutable spatial domain. Single concrete impl that backs the
 * placed-modules tracker for generative growth runs -- shape-agnostic by
 * construction:
 *
 *   - Storage: parallel arrays of (FInstancedStruct shape payload, owner idx,
 *     cached narrow-phase tag, validity bit)
 *   - Broadphase: FDynamicCollection lensed as an AABB octree -- each entry's
 *     WorldAABB is added as an identity-orientation OBB, so the collection's
 *     SAT path collapses to AABB-vs-AABB (cheap, tight, no shape-specific math)
 *   - Narrow phase: PCGExSpatial::NarrowPhase 2D dispatch table, indexed by
 *     (CandidateKindTag, StoredKindTag) -- one array index + one branch per
 *     surviving pair.
 *
 * Adding a new shape kind requires zero edits here. Register pair tests via
 * NarrowPhase::Register from the shape's owning module's StartupModule;
 * the broadphase routes automatically once the kind has a tag.
 *
 * Owner identity:
 *   The OBB's Bounds.Index slot holds the entry's STORAGE index (into Entries[])
 *   -- not the owner index. Owner index is stored alongside the shape payload.
 *   The skip-by-owner ShouldSkip predicate lambda receives storage idx and
 *   resolves owner via Entries[idx].OwnerIndex internally; outward API still
 *   takes owner index from callers, matching the FPCGExSpatialDomain contract.
 *
 * Snapshot model:
 *   BeginSnapshotScope() returns Entries.Num() (high-water mark).
 *   RollbackToScope(handle) flips ValidMask bits past handle to invalid.
 *   O(1) amortized, no realloc, no per-entry walk on partial rollback.
 *
 *   InvalidateOwner / RevalidateOwner flip a single owner's bits anywhere in the array, resolved
 *   through EntriesByOwner rather than a full scan -- retraction runs per placement and grammar
 *   pruning cascades, so a scan would make a K-placement subtree retraction O(K * N). Only THIS
 *   mask is touched -- the broadphase collection's own ValidMask is left alone, because every
 *   query routes each culled entry through a SkipPredicate that already consults
 *   ValidMask[StorageIdx].
 *
 *   GetBounds() may return an over-approximation after partial rollback:
 *   invalid entries' AABBs aren't subtracted out (that'd be O(N)). Consumers
 *   must treat GetBounds() as a cull hint, never a tight extent. Full rollback
 *   (Handle == 0) resets to empty.
 */
class PCGEXSPATIALDOMAINS_API FPCGExSpatialDomain_Broadphase : public FPCGExSpatialDomain
{
public:
	FPCGExSpatialDomain_Broadphase();
	virtual ~FPCGExSpatialDomain_Broadphase() override = default;

	/**
	 * Override the channel-interaction matrix used for query-time gating.
	 * Default is `UPCGExSpatialDomainsSettings::GetCompiledMatrix()` at
	 * construction; tests and isolated growth runs that want a different
	 * matrix call this before placing anything.
	 */
	void SetChannelMatrix(const FPCGExChannelInteractionMatrix& InMatrix)
	{
		MatrixRef = &InMatrix;
	}

	// ========== Query ==========

	virtual float QueryPoint(const FVector& Point) const override;

	// Unified shape-agnostic queries -- the canonical path.
	virtual bool Overlaps(
		const FPCGExFootprintShape& Candidate,
		int32 SkipOwnerIndex,
		TFunctionRef<bool(int32)> ShouldSkip,
		uint32 CandidateChannelMask = 0) const override;
	using FPCGExSpatialDomain::Overlaps; // pull in 2-arg no-skip overload

	virtual bool OverlapsBeyondThreshold(
		const FPCGExFootprintShape& Candidate,
		float MaxAllowedPenetration,
		int32 SkipOwnerIndex = INDEX_NONE,
		uint32 CandidateChannelMask = 0) const override;

	virtual FBox GetBounds() const override
	{
		return WorldBounds;
	}

	virtual bool IsValid() const override
	{
		return NumValidEntries > 0;
	}

	// ========== Mutation + snapshot ==========

	virtual bool IsMutable() const override
	{
		return true;
	}

	virtual int32 Append(
		const FPCGExFootprintShape& Shape,
		int32 OwnerIndex,
		uint32 ChannelMask = 0) override;
	virtual void Reserve(int32 ExpectedCount) override;
	virtual void BeginBulkAppend() override;
	virtual void EndBulkAppend() override;
	virtual FSnapshotHandle BeginSnapshotScope() override;
	virtual void RollbackToScope(FSnapshotHandle Handle) override;
	virtual int32 InvalidateOwner(int32 OwnerIndex) override;
	virtual int32 RevalidateOwner(int32 OwnerIndex) override;

	// ========== Inspection (tests / debug) ==========

	int32 Num() const
	{
		return Entries.Num();
	}

	int32 NumValid() const
	{
		return NumValidEntries;
	}

private:
	struct FEntry
	{
		FInstancedStruct Shape;
		int32 OwnerIndex = INDEX_NONE;
		FBox WorldAABB = FBox(ForceInit);

		/**
		 * Cached narrow-phase tag for this entry's shape kind. Resolved once
		 * at Append time so per-pair dispatch is a single 2D array index --
		 * no GetScriptStruct() walk or hash per overlap test.
		 */
		PCGExSpatial::NarrowPhase::FShapeKindTag KindTag = PCGExSpatial::NarrowPhase::InvalidKindTag;

		/**
		 * Bitmask over the project's channel registry. 0 = no channel info
		 * (the broadphase's matrix-gate query falls back to "run narrow
		 * phase" -- preserves pre-channel-matrix behavior for un-channeled
		 * entries). Set by Append; immutable for the entry's lifetime.
		 */
		uint32 ChannelMask = 0;
	};

	TArray<FEntry> Entries;
	TBitArray<> ValidMask;
	int32 NumValidEntries = 0;

	/**
	 * Owner index -> the storage indices that owner contributed. Entries is append-only (rollback
	 * and invalidation only flip ValidMask), so an index recorded here stays addressable for the
	 * domain's lifetime and may name an already-invalid slot; both flip paths consult ValidMask
	 * before touching one, which is what keeps this a pure index with no semantics of its own.
	 */
	TMap<int32, TArray<int32>> EntriesByOwner;

	/**
	 * Union AABB of all entries that have ever been appended (never shrunk on
	 * partial rollback -- see class doc). Reset to empty on full rollback
	 * (Handle == 0). Used as a cheap cull by GetBounds() consumers.
	 */
	FBox WorldBounds = FBox(ForceInit);

	/**
	 * Broadphase backing -- FDynamicCollection used as an AABB octree.
	 * Each entry's WorldAABB enters as an identity-orientation FOBB
	 * centered on the AABB; collection's SAT path then collapses to
	 * AABB-vs-AABB. Bounds.Index = storage index (into Entries[]).
	 */
	PCGExMath::OBB::FDynamicCollection BroadphaseAABBs;

	/**
	 * BroadphaseAABBs' rebuild cadence saved by BeginBulkAppend, restored by
	 * EndBulkAppend. INDEX_NONE = not inside a bulk scope (the cadence is a
	 * positive count, so the sentinel can't collide with a real value).
	 */
	int32 BulkSavedRebuildInterval = INDEX_NONE;

	/** Entry count at BeginBulkAppend -- an unchanged count means there is nothing to index. */
	int32 BulkEntryCountAtBegin = 0;

	/**
	 * Borrowed-pointer to the channel-interaction matrix consulted before
	 * each narrow-phase invocation. Initialized to the project-settings'
	 * compiled matrix in the constructor; tests/specialized growth runs
	 * may override via SetChannelMatrix. Pointer (not reference) so we
	 * can keep the default ctor noexcept-friendly; guaranteed non-null
	 * post-construction.
	 */
	const FPCGExChannelInteractionMatrix* MatrixRef = nullptr;
};
