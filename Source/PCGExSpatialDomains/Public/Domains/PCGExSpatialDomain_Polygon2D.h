// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Domains/PCGExSpatialDomain.h"

/**
 * Static spatial domain backed by a 2D polygon outline + height band,
 * authored on an arbitrary projection plane.
 *
 * Represents an extruded prism: a 2D outline lives in the XY of a
 * projection frame (rotation-only, no translation), with a Z band along
 * the projection-frame's normal. The default projection is identity, so
 * authored verts treated as world-XY behave as expected; for splines /
 * BestFitPlane sources the projection rotates the outline into the
 * spline's plane so queries respect the original spatial orientation.
 *
 * Common use: room floor plans on tilted ground planes, building
 * outlines, level-chunk perimeters where the source spline isn't
 * world-axis-aligned.
 *
 * Storage is owned (TArray<FVector2D> outline + FQuat projection +
 * ZMin/ZMax) -- decoupled from any backing PCG point data, so the domain
 * outlives the spline/PointIO it was baked from. MakeFromOutline accepts
 * authored 2D verts directly with an optional explicit projection.
 *
 * Topology: arbitrary (concave allowed). Inside test delegates to
 * winding-number math. Closest-edge distance is naive O(N) -- fine for
 * typical room outlines (10-50 edges); add an edge octree later if
 * profiling justifies.
 *
 * Signed distance follows the unified convention (negative inside).
 *
 * Mutability: false. Append() check(false)s per the static-subclass policy.
 */
class PCGEXSPATIALDOMAINS_API FPCGExSpatialDomain_Polygon2D : public FPCGExSpatialDomain
{
public:
	FPCGExSpatialDomain_Polygon2D() = default;
	virtual ~FPCGExSpatialDomain_Polygon2D() override = default;

	// ========== Construction ==========

	/**
	 * Bake from authored 2D outline + Z band, with an optional projection
	 * frame. Identity quat (default) means "outline is in world XY, Z
	 * band is along world Z" -- the simple case. Pass a non-identity quat
	 * when the outline was authored in a tilted/rotated frame.
	 *
	 * Outline winding doesn't matter for inside test; both CW and CCW work.
	 */
	static FPCGExSpatialDomain_Polygon2D MakeFromOutline(
		TArray<FVector2D> Outline,
		float ZMin,
		float ZMax,
		const FQuat& ProjectionQuat = FQuat::Identity);

	// ========== FPCGExSpatialDomain ==========

	virtual float QueryPoint(const FVector& Point) const override;

	virtual FBox GetBounds() const override
	{
		return WorldBounds;
	}

	virtual bool IsValid() const override
	{
		return Outline.Num() >= 3 && ZMax > ZMin;
	}

	virtual int32 Append(const FPCGExFootprintShape& Shape, int32 OwnerIndex, uint32 ChannelMask = 0) override;

	// ========== Inspection ==========

	const TArray<FVector2D>& GetOutline() const
	{
		return Outline;
	}

	const FQuat& GetProjectionQuat() const
	{
		return ProjectionQuat;
	}

	float GetZMin() const
	{
		return ZMin;
	}

	float GetZMax() const
	{
		return ZMax;
	}

private:
	/** Authored 2D outline in projection-frame XY. Winding-agnostic for inside test. */
	TArray<FVector2D> Outline;

	/**
	 * Rotation from world space INTO the projection frame. Identity = world XY.
	 * QueryPoint applies UnrotateVector(WorldPoint) to get a frame-local 3D
	 * point; the resulting (X, Y) is the 2D coord against Outline, and Z is
	 * the height-band axis.
	 */
	FQuat ProjectionQuat = FQuat::Identity;

	/** Height band along the projection frame's local Z. Inclusive on both ends. */
	float ZMin = 0.0f;
	float ZMax = 0.0f;

	/**
	 * Cached world-space 3D AABB. Computed by un-projecting the 8 corners of
	 * the local AABB (Outline extents + ZMin/ZMax) through ProjectionQuat and taking
	 * min/max -- conservative when ProjectionQuat is non-identity.
	 */
	FBox WorldBounds = FBox(ForceInit);

	/** Recompute WorldBounds from Outline + (ZMin, ZMax) + ProjectionQuat. */
	void RecomputeBounds();
};
