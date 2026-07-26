// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/Geo/PCGExGeo.h"
#include "Math/OBB/PCGExOBB.h"
#include "NarrowPhase/PCGExNarrowPhase.h"
#include "NarrowPhase/PCGExNarrowPhaseRegistrations.h"
#include "Shapes/PCGExFootprintShape.h"

namespace PCGExSpatial::NarrowPhase
{
	namespace PolygonPair
	{
		/**
		 * OBB-vs-Polygon precise overlap test: project the OBB shadow into
		 * the polygon's projection frame, reject on Z band, then run 2D
		 * polygon-vs-shadow overlap (concave-safe). World-AABB pre-cull
		 * lives at the broadphase tier -- by the time this runs the
		 * broadphase has already accepted the pair as AABB-overlapping.
		 */
		bool OBBvsPolygon_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& OBB = static_cast<const FPCGExFootprintShape_OBB&>(A);
			const auto& Polygon = static_cast<const FPCGExFootprintShape_Polygon&>(B);
			const FPCGExSpatialPolygonEntry& Entry = Polygon.Entry;

			TArray<FVector2D, TInlineAllocator<8>> Shadow;
			float ShadowZMin, ShadowZMax;
			PCGExMath::Geo::ProjectOBBToFrame(
				OBB.Bounds, Entry.WorldOrigin, Entry.ProjectionQuat,
				Shadow, ShadowZMin, ShadowZMax);

			if (ShadowZMax < Entry.ZMin || ShadowZMin > Entry.ZMax)
			{
				return false;
			}
			return PCGExMath::Geo::PolygonsOverlap2D(Entry.Outline, Shadow);
		}

		/**
		 * Polygon-vs-Polygon precise overlap test: project candidate prism
		 * into stored polygon's frame, reject on Z band, run 2D concave-vs-
		 * concave overlap.
		 */
		bool PolygonVsPolygon_Overlap(const FPCGExFootprintShape& A, const FPCGExFootprintShape& B)
		{
			const auto& PolyA = static_cast<const FPCGExFootprintShape_Polygon&>(A);
			const auto& PolyB = static_cast<const FPCGExFootprintShape_Polygon&>(B);
			const FPCGExSpatialPolygonEntry& Candidate = PolyA.Entry;
			const FPCGExSpatialPolygonEntry& Stored = PolyB.Entry;

			// Inline storage sized for typical floor-plan outlines; falls back
			// to heap on overflow. Avoids per-call allocation in the hot path.
			TArray<FVector2D> CandidateInStored;
			float CandidateZMin, CandidateZMax;
			PCGExMath::Geo::ProjectPrismToFrame(
				Candidate.Outline, Candidate.ZMin, Candidate.ZMax,
				Candidate.WorldOrigin, Candidate.ProjectionQuat,
				Stored.WorldOrigin, Stored.ProjectionQuat,
				CandidateInStored, CandidateZMin, CandidateZMax);

			if (CandidateZMax < Stored.ZMin || CandidateZMin > Stored.ZMax)
			{
				return false;
			}
			return PCGExMath::Geo::PolygonsOverlap2D(Stored.Outline, CandidateInStored);
		}

		float Polygon_QueryPoint(const FVector& Point, const FPCGExFootprintShape& Stored)
		{
			const FPCGExSpatialPolygonEntry& Entry = static_cast<const FPCGExFootprintShape_Polygon&>(Stored).Entry;
			return PCGExMath::Geo::SignedDistanceToPolygonPrism(
				Entry.ProjectionQuat.UnrotateVector(Point - Entry.WorldOrigin),
				TConstArrayView<FVector2D>(Entry.Outline),
				Entry.ZMin, Entry.ZMax);
		}
	}

	void RegisterPolygonPairTests()
	{
		// OBB-vs-Polygon. Stored under one direction; the registry's
		// symmetric Resolve() handles arg-swap automatically when the
		// query comes in the other direction.
		//
		// Penetration is intentionally NULL on polygon pairs (current
		// limitation):
		//
		//   The registry's QueryPenetration falls back to +INFINITY when
		//   there's no Penetration fn -- meaning "any overlap exceeds any
		//   threshold". That's the correct-direction conservative default:
		//   FootprintPenetration placement conditions reject the candidate
		//   on any polygon overlap. It is NOT looser than expected; it is
		//   NOT incorrect; it is just missing the "tolerate shallow polygon
		//   overlaps" capability.
		//
		//   Naive conservative paths (polygon's bounding OBB vs OBB SAT-MTV)
		//   are wrong-direction for concave polygons: an L-shape with a
		//   candidate in the inner corner gets reported as deeply penetrated
		//   when the actual polygon-vs-OBB penetration is zero. Authors
		//   reach for polygons specifically to allow placement in concave
		//   negative space; over-rejecting kills that benefit.
		//
		//   Real polygon-prism vs OBB MTV (2D SAT on the outline + Z-band
		//   depth, concave-aware) is ~half a day of math. We'll implement
		//   it when a real user need surfaces, or via the natural extension
		//   path: a separate shape type (FPCGExFootprintShape_PrecisePolygon2D)
		//   that opts into the precise math while the existing _Polygon
		//   keeps its cheap default. Adding it is a pure addition -- new
		//   USTRUCT + Register calls, no edits here.
		Register(
			FPCGExFootprintShape_OBB::StaticStruct(),
			FPCGExFootprintShape_Polygon::StaticStruct(),
			{&PolygonPair::OBBvsPolygon_Overlap, /*Penetration*/ nullptr});

		// Polygon-vs-Polygon. Same Penetration story as above.
		Register(
			FPCGExFootprintShape_Polygon::StaticStruct(),
			FPCGExFootprintShape_Polygon::StaticStruct(),
			{&PolygonPair::PolygonVsPolygon_Overlap, /*Penetration*/ nullptr});

		RegisterQueryPoint(
			FPCGExFootprintShape_Polygon::StaticStruct(),
			&PolygonPair::Polygon_QueryPoint);
	}
}
