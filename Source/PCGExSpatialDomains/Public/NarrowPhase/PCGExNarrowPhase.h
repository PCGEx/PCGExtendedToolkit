// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Shapes/PCGExFootprintShape.h"

namespace PCGExSpatial::NarrowPhase
{
	/**
	 * Stable per-module-lifetime tag for a registered shape kind. The hot
	 * dispatch path indexes a 2D function-pointer table by (StoredTag, CandidateTag);
	 * callers (e.g. the broadphase) cache the tag on each stored entry at
	 * Append time so per-pair-test lookup is a single 2D array index.
	 *
	 * Tags are dense and stable for the module's lifetime. Never re-assigned.
	 */
	using FShapeKindTag = int32;
	constexpr FShapeKindTag InvalidKindTag = INDEX_NONE;

	/**
	 * Pair-test function signatures. Free functions, registered by shape pair
	 * at module-init time. Receive both operands as base-class refs; impls
	 * static_cast to their concrete shape types -- the cast is type-safe by
	 * construction (the registry only invokes the fn when both tags match).
	 */
	using FPairOverlapFn = bool (*)(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B);
	using FPairPenetrationFn = float (*)(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B);

	/**
	 * Single-shape signed-distance signature. Returns negative inside, positive
	 * outside, zero on surface. Used by FPCGExSpatialDomain_Broadphase::QueryPoint
	 * to walk stored entries and combine via min (CSG-union semantics), and by
	 * placement conditions that need precise polygon / OBB distance against an
	 * external constraint domain.
	 *
	 * Registered per stored kind (not per pair) -- the query takes a single
	 * shape and a world-space point.
	 */
	using FQueryPointFn = float (*)(const FVector& Point, const FPCGExFootprintShape& Stored);

	/**
	 * Per-pair function bundle. Penetration is optional -- when null, the
	 * registry's QueryPenetration() falls back to a binary "any overlap is
	 * infinite penetration" semantic via Overlap.
	 */
	struct PCGEXSPATIALDOMAINS_API FPairFns
	{
		FPairOverlapFn Overlap = nullptr;
		FPairPenetrationFn Penetration = nullptr;
	};

	/**
	 * Ensure a stable tag exists for the given shape kind. Idempotent: callers
	 * may invoke from arbitrary registration sites without worrying about
	 * double-tagging. Returns InvalidKindTag iff Struct is null.
	 *
	 * Register() auto-tags both operands, so most consumers never call this
	 * directly. Use it when a shape kind needs a tag without (yet) having any
	 * pair tests registered -- e.g. a storage backend that wants to cache tags
	 * before pair-test modules have loaded.
	 *
	 * Thread model: call from module-init (StartupModule); never call from
	 * worker threads.
	 */
	PCGEXSPATIALDOMAINS_API FShapeKindTag RegisterShapeKind(UScriptStruct* Struct);

	/**
	 * Resolve the tag previously assigned to a shape kind. Returns InvalidKindTag
	 * for kinds that have never been registered (directly or through Register()).
	 *
	 * Read-only; safe to call from any thread post-registration.
	 */
	PCGEXSPATIALDOMAINS_API FShapeKindTag FindShapeKindTag(const UScriptStruct* Struct);

	/**
	 * Register the pair test for (StructA, StructB). Auto-registers tags for
	 * both kinds (no need to call RegisterShapeKind first). The dispatch table
	 * stores a mirrored entry at [TagB][TagA] with an arg-swap flag, so
	 * lookups in either direction take a single 2D array index.
	 *
	 * Registering the same pair twice is a programmer error: ensures in
	 * debug, last-write-wins in shipping. Registering both orientations of
	 * the same pair (i.e. (A, B) and then (B, A)) is also flagged -- impls
	 * depend on a consistent argument layout, so only one direction may be
	 * registered.
	 *
	 * Either of the Fns members may be null. A null Overlap effectively
	 * disables the pair (TestOverlap will return false / not-overlapping);
	 * a null Penetration falls back to Overlap.
	 *
	 * Thread model: call from StartupModule; queries from any phase after
	 * that are safe.
	 */
	PCGEXSPATIALDOMAINS_API void Register(
		UScriptStruct* StructA,
		UScriptStruct* StructB,
		FPairFns Fns);

	/** Drop all registrations (kinds AND pair tests). Test/utility hook -- production code never calls this. */
	PCGEXSPATIALDOMAINS_API void UnregisterAll();

	/**
	 * The full built-in registration list. Anything that calls UnregisterAll()
	 * must restore through this -- the registry is one process-wide instance, so a
	 * hand-maintained restore that misses a kind disables it session-wide.
	 */
	PCGEXSPATIALDOMAINS_API void RegisterBuiltInPairTests();

	/**
	 * Tag-dispatched overlap query. The fast path -- single 2D array index +
	 * one branch (for the swap flag) per pair test. Callers cache tags on
	 * their stored entries (see broadphase FEntry::KindTag); the candidate's
	 * tag is resolved once per overlap query outside the inner loop.
	 *
	 * Tags must be valid (>= 0 and bounded). Out-of-range tags trip a check.
	 */
	PCGEXSPATIALDOMAINS_API bool TestOverlap(
		FShapeKindTag AKind, const FPCGExFootprintShape& A,
		FShapeKindTag BKind, const FPCGExFootprintShape& B);

	PCGEXSPATIALDOMAINS_API float QueryPenetration(
		FShapeKindTag AKind, const FPCGExFootprintShape& A,
		FShapeKindTag BKind, const FPCGExFootprintShape& B);

	/**
	 * Convenience overloads: tags are resolved via GetScriptStruct() on each
	 * side. Use in cold paths (one-off tests, generic helpers, debug hooks).
	 * Return false / +INFINITY when either side's kind has never been
	 * registered.
	 */
	PCGEXSPATIALDOMAINS_API bool TestOverlap(
		const FPCGExFootprintShape& A,
		const FPCGExFootprintShape& B);

	PCGEXSPATIALDOMAINS_API float QueryPenetration(
		const FPCGExFootprintShape& A,
		const FPCGExFootprintShape& B);

	/**
	 * Register the signed-distance function for a stored shape kind. Auto-
	 * registers the tag (no need to call RegisterShapeKind first). Calling
	 * twice for the same kind is a programmer error: ensures in debug,
	 * last-write-wins in shipping.
	 *
	 * Thread model: call from StartupModule; queries from any phase after
	 * that are safe.
	 */
	PCGEXSPATIALDOMAINS_API void RegisterQueryPoint(
		UScriptStruct* Struct,
		FQueryPointFn Fn);

	/**
	 * True when the shape kind has a signed-distance implementation. Check before
	 * trusting QueryPoint's +INFINITY default: "maximally outside" is only safe
	 * when another entry in the channel can answer -- for an all-overlap-only
	 * channel it inverts inside/outside tests.
	 */
	PCGEXSPATIALDOMAINS_API bool HasQueryPoint(const UScriptStruct* Struct);

	/**
	 * Tag-dispatched signed-distance query. The hot path used by
	 * FPCGExSpatialDomain_Broadphase::QueryPoint -- single table read +
	 * one indirect call per stored entry. Returns +INFINITY for unregistered
	 * kinds (see HasQueryPoint -- that default is NOT safe in isolation).
	 *
	 * Tag must be valid (>= 0 and bounded). Out-of-range tags trip a check.
	 */
	PCGEXSPATIALDOMAINS_API float QueryPoint(
		FShapeKindTag StoredKind,
		const FVector& Point,
		const FPCGExFootprintShape& Stored);

	/**
	 * Convenience overload: tag resolved via GetScriptStruct(). Cold-path
	 * use; returns +INFINITY when the stored kind has never been registered.
	 */
	PCGEXSPATIALDOMAINS_API float QueryPoint(
		const FVector& Point,
		const FPCGExFootprintShape& Stored);
}
