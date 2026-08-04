// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Math/OBB/PCGExOBB.h"
#include "Shapes/PCGExFootprintShape.h"

/**
 * Abstract spatial domain -- a queryable region for constraining placement
 * decisions. Domain-agnostic on the consumer side: callers pass a shape via
 * FPCGExFootprintShape and ask "does this overlap?" without knowing which
 * concrete impl backs the query.
 *
 * Subclass strategy:
 *   - Broadphase: heterogeneous mutable tracker, AABB-octree backed; the
 *     placed-modules domain in growth runs.
 *   - Polygon2D: static, single extruded prism (floor plans, room outlines).
 *   - SDF: static, voxelized signed-distance field (Phase-3 stub).
 *
 * Overlap math is shape-pair-typed and lives in PCGExSpatial::NarrowPhase --
 * adding a new shape kind is a pure addition (new shape USTRUCT + register
 * pair tests from a StartupModule). No edits to existing domains required.
 *
 * Convention: signed distance is negative inside, positive outside, zero on
 * surface. CSG-friendly: union = min(a,b), intersect = max(a,b),
 * subtract = max(a,-b).
 *
 * Lives in PCGExSpatialDomains -- generic infrastructure, not valency-
 * specific. Consumer modules add this as a public dependency.
 */
class PCGEXSPATIALDOMAINS_API FPCGExSpatialDomain
{
public:
	virtual ~FPCGExSpatialDomain();

	/** Shared no-op skip predicate -- the default for Overlaps* family calls. */
	static bool NoSkip(int32)
	{
		return false;
	}

	// ========== Query API ==========

	/**
	 * Query signed distance at a world-space point. Negative inside,
	 * positive outside, zero on surface.
	 */
	virtual float QueryPoint(const FVector& Point) const = 0;

	/**
	 * Conservative OBB query -- samples center + 8 corners (9 points).
	 * Returns minimum signed distance across all samples ("most inside").
	 *
	 * Subclasses may override with a tighter bound for their representation.
	 */
	virtual float QueryOBB(const PCGExMath::OBB::FOBB& Bounds) const;

	/**
	 * Unified shape-agnostic overlap query. The candidate carries its kind
	 * via UScriptStruct; mutable domains (Broadphase) dispatch per stored
	 * entry through PCGExSpatial::NarrowPhase. Returns true on overlap.
	 *
	 * Default impl: signed-distance fallback against the candidate's
	 * bounding OBB derived from its WorldAABB -- safe and useful for static
	 * domains (Polygon2D, SDF) which inherit it. The Broadphase concrete
	 * overrides with the precise registry-driven dispatch.
	 *
	 * @param Candidate       Candidate shape (any kind).
	 * @param SkipOwnerIndex  Owner index whose stored contributions to skip;
	 *                        INDEX_NONE = skip nothing.
	 * @param ShouldSkip      Per-element predicate. Receives the stored
	 *                        entry's owner index; return true to skip.
	 */
	virtual bool Overlaps(
		const FPCGExFootprintShape& Candidate,
		int32 SkipOwnerIndex,
		TFunctionRef<bool(int32)> ShouldSkip,
		uint32 CandidateChannelMask = 0) const;

	/** No-skip-predicate convenience overload. Forwards to the virtual. */
	bool Overlaps(
		const FPCGExFootprintShape& Candidate,
		int32 SkipOwnerIndex = INDEX_NONE,
		uint32 CandidateChannelMask = 0) const
	{
		return Overlaps(Candidate, SkipOwnerIndex, NoSkip, CandidateChannelMask);
	}

	/**
	 * Penetration-aware overlap. Returns true when the candidate penetrates
	 * any stored entry beyond MaxAllowedPenetration. Default impl shims to
	 * boolean Overlaps (any overlap counts as exceeding any threshold).
	 * Broadphase overrides via the registry's QueryPenetration path.
	 *
	 * CandidateChannelMask gates the test through the channel-interaction
	 * matrix (Ignored pairs are skipped without invoking narrow phase).
	 * Default 0 = no channel info -> matrix-gate falls back to "always run".
	 */
	virtual bool OverlapsBeyondThreshold(
		const FPCGExFootprintShape& Candidate,
		float MaxAllowedPenetration,
		int32 SkipOwnerIndex = INDEX_NONE,
		uint32 CandidateChannelMask = 0) const;

	// ========== Common ==========

	/** World-space bounding box. Used for early rejection before detailed queries. */
	virtual FBox GetBounds() const = 0;

	/** True if this domain has been built and is ready for queries. */
	virtual bool IsValid() const = 0;

	// ========== Mutation API (mutable subclasses only) ==========

	/**
	 * True if this domain accepts Append() / snapshot mutations. Static
	 * subclasses (Polygon2D, SDF, ...) return false; the placed-modules
	 * tracker (Broadphase) returns true.
	 */
	virtual bool IsMutable() const
	{
		return false;
	}

	/**
	 * Append a shape with its owner identity + channel mask. Pure virtual --
	 * every concrete subclass implements (the mutable Broadphase actually
	 * stores; static subclasses like Polygon2D / SDF check(false) loudly so
	 * generic callers get a clear failure rather than a silent no-op).
	 *
	 * OwnerIndex must be >= 0 -- INDEX_NONE is the skip-nothing sentinel.
	 *
	 * ChannelMask is a bitmask over the project's channel registry (see
	 * UPCGExSpatialDomainsSettings::SpatialChannels). Bit N set = the entry
	 * participates in the channel at index N. 0 = no channel info (the
	 * broadphase's matrix-gate falls back to "always run narrow phase" --
	 * preserves pre-channel behaviour for un-channeled entries).
	 */
	virtual int32 Append(
		const FPCGExFootprintShape& Shape,
		int32 OwnerIndex,
		uint32 ChannelMask = 0) = 0;

	/**
	 * Hint the expected entry count so mutable subclasses can pre-size their
	 * storage. Optional -- default no-op so static subclasses (Polygon2D, SDF)
	 * inherit the empty implementation without having to opt out.
	 *
	 * Call once before a batched Append sequence when the count is known
	 * (placement-op setup); calling mid-sequence is allowed but pointless.
	 */
	virtual void Reserve(int32 ExpectedCount)
	{
	}

	/**
	 * Bulk-append scope. Between these calls a mutable domain may suspend the
	 * incremental maintenance of its query accelerator, so a known-size batch
	 * pays one index build instead of one every N appends.
	 *
	 * CONTRACT: no query may run inside the scope -- the accelerator is stale
	 * until EndBulkAppend. Calls do not nest; a second Begin is ignored, and an
	 * End without a matching Begin is a no-op. Default no-op so static
	 * subclasses (Polygon2D, SDF) inherit it without opting out.
	 */
	virtual void BeginBulkAppend()
	{
	}

	virtual void EndBulkAppend()
	{
	}

	// ========== Snapshot API ==========

	/**
	 * Opaque per-domain snapshot handle. The mutable broadphase stores an
	 * int32 high-water mark; static domains return 0. Round-trip:
	 * BeginSnapshotScope() captures, RollbackToScope() restores.
	 */
	using FSnapshotHandle = int32;

	virtual FSnapshotHandle BeginSnapshotScope()
	{
		return 0;
	}

	virtual void RollbackToScope(FSnapshotHandle Handle)
	{
	}
};
