// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Shapes/PCGExFootprintShape.h"

// =============================================================================
// FPCGExFootprintShape (base)
// =============================================================================

// Out-of-line destructor: forces a single export of the vtable + dtor symbol
// from this module. Without this, consuming modules that construct/destroy
// subclasses inline get LNK2019 unresolved __imp_ symbols.
FPCGExFootprintShape::~FPCGExFootprintShape() = default;

// =============================================================================
// FPCGExFootprintShape_OBB
// =============================================================================

FPCGExFootprintShape_OBB::FPCGExFootprintShape_OBB() = default;

FPCGExFootprintShape_OBB::FPCGExFootprintShape_OBB(const PCGExMath::OBB::FOBB& InBounds)
	: Bounds(InBounds)
{
}

FPCGExFootprintShape_OBB::~FPCGExFootprintShape_OBB() = default;

FBox FPCGExFootprintShape_OBB::GetWorldAABB() const
{
	// Hot path -- the broadphase calls this on the candidate per Overlaps(), i.e.
	// per lattice cell and per placement attempt. Not cached: callers mutate
	// Bounds.Origin in place between queries.
	const FVector& Extents = Bounds.GetExtents();
	const FQuat& Rotation = Bounds.GetRotation();
	const FVector HalfSize =
		Rotation.GetAxisX().GetAbs() * Extents.X +
		Rotation.GetAxisY().GetAbs() * Extents.Y +
		Rotation.GetAxisZ().GetAbs() * Extents.Z;

	const FVector& Origin = Bounds.GetOrigin();
	return FBox(Origin - HalfSize, Origin + HalfSize);
}

// =============================================================================
// FPCGExFootprintShape_Polygon
// =============================================================================

FPCGExFootprintShape_Polygon::FPCGExFootprintShape_Polygon() = default;

FPCGExFootprintShape_Polygon::FPCGExFootprintShape_Polygon(FPCGExSpatialPolygonEntry InEntry)
	: Entry(MoveTemp(InEntry))
{
}

FPCGExFootprintShape_Polygon::~FPCGExFootprintShape_Polygon() = default;

// =============================================================================
// FPCGExFootprintShape_Volume
// =============================================================================
//
// AVolume / UPrimitiveComponent stay incomplete here: TWeakObjectPtr is
// type-erased, so copy + destroy need no Engine headers.

FPCGExFootprintShape_Volume::FPCGExFootprintShape_Volume() = default;

FPCGExFootprintShape_Volume::FPCGExFootprintShape_Volume(const TWeakObjectPtr<AVolume>& InVolume, const FBox& InWorldAABB)
	: Volume(InVolume)
	  , WorldAABB(InWorldAABB)
{
}

FPCGExFootprintShape_Volume::~FPCGExFootprintShape_Volume() = default;

// =============================================================================
// FPCGExFootprintShape_Primitive
// =============================================================================

FPCGExFootprintShape_Primitive::FPCGExFootprintShape_Primitive() = default;

FPCGExFootprintShape_Primitive::FPCGExFootprintShape_Primitive(const TWeakObjectPtr<UPrimitiveComponent>& InPrimitive, const FBox& InWorldAABB)
	: Primitive(InPrimitive)
	  , WorldAABB(InWorldAABB)
{
}

FPCGExFootprintShape_Primitive::~FPCGExFootprintShape_Primitive() = default;
