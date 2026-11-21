// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Domains/PCGExSpatialDomain.h"

#include "Shapes/PCGExFootprintShape.h"

// Out-of-line virtual destructor anchors the vtable in this TU. UE pattern
// for _API-marked polymorphic classes -- inline `= default` would risk no-emit
// when consuming modules need to reference the destructor symbol.
FPCGExSpatialDomain::~FPCGExSpatialDomain() = default;

float FPCGExSpatialDomain::QueryOBB(const PCGExMath::OBB::FOBB& Bounds) const
{
	// Default: sample center + 8 corners, return minimum (most "inside").
	// Conservative for MustBeOutside; MustBeInside callers also need the max,
	// so they sample corners themselves rather than going through QueryOBB.
	float MinDist = QueryPoint(Bounds.GetOrigin());
	Bounds.ForEachCorner([&](const FVector& World)
	{
		MinDist = FMath::Min(MinDist, QueryPoint(World));
	});
	return MinDist;
}

bool FPCGExSpatialDomain::Overlaps(
	const FPCGExFootprintShape& Candidate,
	[[maybe_unused]] int32 SkipOwnerIndex,
	[[maybe_unused]] TFunctionRef<bool(int32)> ShouldSkip,
	[[maybe_unused]] uint32 CandidateChannelMask) const
{
	// Generic fallback: signed-distance overlap against the candidate's
	// bounding OBB derived from its WorldAABB. Useful for static domains
	// (Polygon2D) which back this with their QueryPoint impls; the
	// mutable Broadphase overrides with the registry-driven precise path.
	//
	// Skip args ignored at this level: static domains have no per-element
	// identity to filter on. The Broadphase override honors them via the
	// per-entry callback in its broadphase walk.
	const FBox AABB = Candidate.GetWorldAABB();
	if (!AABB.IsValid)
	{
		return false;
	}

	return QueryOBB(PCGExMath::OBB::Factory::FromAABB(AABB, INDEX_NONE)) <= 0.0f;
}

bool FPCGExSpatialDomain::OverlapsBeyondThreshold(
	const FPCGExFootprintShape& Candidate,
	[[maybe_unused]] float MaxAllowedPenetration,
	int32 SkipOwnerIndex,
	uint32 CandidateChannelMask) const
{
	// Conservative shim: any overlap exceeds any threshold. The Broadphase
	// overrides with the registry's QueryPenetration path which returns
	// real MTV magnitudes for OBB-OBB pairs.
	return Overlaps(Candidate, SkipOwnerIndex, NoSkip, CandidateChannelMask);
}
