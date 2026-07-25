// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/OBB/PCGExOBB.h"
#include "NarrowPhase/PCGExNarrowPhase.h"
#include "NarrowPhase/PCGExNarrowPhaseRegistrations.h"
#include "Shapes/PCGExFootprintShape.h"

#include "CollisionShape.h"
#include "Components/BrushComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Volume.h"

// AVolume's brush IS a UPrimitiveComponent, so both shapes answer the same exact
// oriented-box question through OverlapComponent.
//
// These physics queries take a Chaos read lock with no game-thread assert, so they
// are worker-safe; the weak-pointer resolution around them is not (Get() reports
// null for a live object during a GC mark phase). TryBake refuses inputs whose
// collision can never answer, so a false here means "no overlap", not "couldn't ask".

namespace PCGExSpatial::NarrowPhase
{
	namespace VolumePrimitivePair
	{
		bool BoxOverlapsComponent(
			const UPrimitiveComponent* Comp,
			const FVector& Origin, const FQuat& Rotation, const FVector& HalfExtents)
		{
			if (!Comp)
			{
				return false;
			}
			return Comp->OverlapComponent(Origin, Rotation, FCollisionShape::MakeBox(FVector3f(HalfExtents)));
		}

		/**
		 * Bounding box in the polygon's own projection frame -- far tighter than its
		 * world AABB for rotated footprints. Exact for rectangular outlines,
		 * conservative for concave ones (over-reports occupancy, never under).
		 */
		PCGExMath::OBB::FOBB PolygonToOBB(const FPCGExSpatialPolygonEntry& Entry)
		{
			FBox2D Outline2D(ForceInit);
			for (const FVector2D& P : Entry.Outline)
			{
				Outline2D += P;
			}

			const FVector2D Center2D = Outline2D.GetCenter();
			const FVector2D Extent2D = Outline2D.GetExtent();
			const FVector LocalCenter(Center2D.X, Center2D.Y, (Entry.ZMin + Entry.ZMax) * 0.5);

			return PCGExMath::OBB::FOBB(
				PCGExMath::OBB::FBounds(
					Entry.WorldOrigin + Entry.ProjectionQuat.RotateVector(LocalCenter),
					FVector(Extent2D.X, Extent2D.Y, (Entry.ZMax - Entry.ZMin) * 0.5),
					INDEX_NONE),
				PCGExMath::OBB::FOrientation(Entry.ProjectionQuat));
		}

		const UPrimitiveComponent* VolumeBrush(const FPCGExFootprintShape_Volume& Vol)
		{
			const AVolume* Actor = Vol.Volume.Get();
			return Actor ? Actor->GetBrushComponent() : nullptr;
		}

		bool OBBvsVolume_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& OBB = static_cast<const FPCGExFootprintShape_OBB&>(A);
			const auto& Vol = static_cast<const FPCGExFootprintShape_Volume&>(B);

			return BoxOverlapsComponent(
				VolumeBrush(Vol),
				OBB.Bounds.GetOrigin(), OBB.Bounds.GetRotation(), OBB.Bounds.GetExtents());
		}

		bool OBBvsPrimitive_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& OBB = static_cast<const FPCGExFootprintShape_OBB&>(A);
			const auto& Prim = static_cast<const FPCGExFootprintShape_Primitive&>(B);

			return BoxOverlapsComponent(
				Prim.Primitive.Get(),
				OBB.Bounds.GetOrigin(), OBB.Bounds.GetRotation(), OBB.Bounds.GetExtents());
		}

		bool PolygonVsVolume_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& Poly = static_cast<const FPCGExFootprintShape_Polygon&>(A);
			const auto& Vol = static_cast<const FPCGExFootprintShape_Volume&>(B);

			const PCGExMath::OBB::FOBB Box = PolygonToOBB(Poly.Entry);
			return BoxOverlapsComponent(
				VolumeBrush(Vol),
				Box.GetOrigin(), Box.GetRotation(), Box.GetExtents());
		}

		bool PolygonVsPrimitive_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& Poly = static_cast<const FPCGExFootprintShape_Polygon&>(A);
			const auto& Prim = static_cast<const FPCGExFootprintShape_Primitive&>(B);

			const PCGExMath::OBB::FOBB Box = PolygonToOBB(Poly.Entry);
			return BoxOverlapsComponent(
				Prim.Primitive.Get(),
				Box.GetOrigin(), Box.GetRotation(), Box.GetExtents());
		}
	}

	void RegisterVolumePrimitivePairTests()
	{
		// Candidate-first; the registry mirrors each pair with an arg-swap flag, so
		// impls always receive their operands in registration order.
		Register(
			FPCGExFootprintShape_OBB::StaticStruct(),
			FPCGExFootprintShape_Volume::StaticStruct(),
			{&VolumePrimitivePair::OBBvsVolume_Overlap, /*Penetration*/ nullptr});

		Register(
			FPCGExFootprintShape_OBB::StaticStruct(),
			FPCGExFootprintShape_Primitive::StaticStruct(),
			{&VolumePrimitivePair::OBBvsPrimitive_Overlap, /*Penetration*/ nullptr});

		// Polygon candidates are live (the Polygon2D footprint element builds them),
		// and an empty slot is not neutral: TestOverlap answers "no overlap" while
		// QueryPenetration answers +INF, so the two conditions would contradict.
		Register(
			FPCGExFootprintShape_Polygon::StaticStruct(),
			FPCGExFootprintShape_Volume::StaticStruct(),
			{&VolumePrimitivePair::PolygonVsVolume_Overlap, /*Penetration*/ nullptr});

		Register(
			FPCGExFootprintShape_Polygon::StaticStruct(),
			FPCGExFootprintShape_Primitive::StaticStruct(),
			{&VolumePrimitivePair::PolygonVsPrimitive_Overlap, /*Penetration*/ nullptr});

		// No RegisterQueryPoint: neither backing object exposes a signed distance
		// (EncompassesPoint's out-distance is unsigned, OverlapComponent is boolean),
		// so there is nothing honest to return. Consumers ask HasQueryPoint instead.
	}
}
