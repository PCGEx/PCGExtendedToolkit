// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Domains/PCGExSpatialDomain_Polygon2D.h"

#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPolyPath.h"

FPCGExSpatialDomain_Polygon2D FPCGExSpatialDomain_Polygon2D::MakeFromOutline(
	TArray<FVector2D> InOutline,
	float InZMin,
	float InZMax,
	const FQuat& InProjectionQuat)
{
	FPCGExSpatialDomain_Polygon2D Out;
	Out.Outline = MoveTemp(InOutline);
	Out.ProjectionQuat = InProjectionQuat;
	// callers may pass Z bounds in arbitrary order; normalize so ZMax >= ZMin
	Out.ZMin = FMath::Min(InZMin, InZMax);
	Out.ZMax = FMath::Max(InZMin, InZMax);
	Out.RecomputeBounds();
	return Out;
}

float FPCGExSpatialDomain_Polygon2D::QueryPoint(const FVector& Point) const
{
	// No WorldOrigin: outline is already in projection-frame XY, so only the
	// quaternion unrotation is needed to bring the world point into local space.
	return PCGExMath::Geo::SignedDistanceToPolygonPrism(
		ProjectionQuat.UnrotateVector(Point),
		TConstArrayView<FVector2D>(Outline),
		ZMin, ZMax);
}

int32 FPCGExSpatialDomain_Polygon2D::Append(const FPCGExFootprintShape& Shape, int32 OwnerIndex, uint32 ChannelMask)
{
	checkf(false, TEXT("FPCGExSpatialDomain_Polygon2D is immutable; Append() is not supported."));
	return INDEX_NONE;
}

void FPCGExSpatialDomain_Polygon2D::RecomputeBounds()
{
	WorldBounds = PCGExMath::Geo::ProjectPrismToWorldAABB(
		Outline, ZMin, ZMax, FVector::ZeroVector, ProjectionQuat);
}
