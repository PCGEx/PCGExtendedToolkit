// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"

#include "PCGExGeo.generated.h"

namespace PCGExMath::OBB
{
	struct FOBB;
}

UENUM()
enum class EPCGExCellCenter : uint8
{
	Balanced     = 0 UMETA(DisplayName = "Balanced", ToolTip="Pick centroid if circumcenter is out of bounds, otherwise uses circumcenter."),
	Circumcenter = 1 UMETA(DisplayName = "Canon (Circumcenter)", ToolTip="Uses Delaunay cells' circumcenter."),
	Centroid     = 2 UMETA(DisplayName = "Centroid", ToolTip="Uses Delaunay cells' averaged vertice positions.")
};

UENUM()
enum class EPCGExVoronoiMetric : uint8
{
	Euclidean = 0 UMETA(DisplayName = "Euclidean (L2)", ToolTip="Standard Euclidean distance. Produces classic Voronoi with straight edges."),
	Manhattan = 1 UMETA(DisplayName = "Manhattan (L1)", ToolTip="Taxicab/Manhattan distance. Produces diamond-shaped cells with axis-aligned and 45-degree edges."),
	Chebyshev = 2 UMETA(DisplayName = "Chebyshev (L-Inf)", ToolTip="Chessboard/Chebyshev distance. Produces square-ish cells with axis-aligned and 45-degree edges.")
};

namespace PCGExMath::Geo
{
	namespace States
	{
		PCGEX_CTX_STATE(State_ExtractingMesh)
	}

	template <typename T>
	FORCEINLINE double Det(const T& A, const T& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	FORCEINLINE static double S_U(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& E, const FVector& F, const FVector& G, const FVector& H)
	{
		return (A.Z - B.Z) * (C.X * D.Y - D.X * C.Y) - (E.Z - F.Z) * (G.X * H.Y - H.X * G.Y);
	};

	FORCEINLINE static double S_D(const int FirstComponent, const int SecondComponent, FVector A, FVector B, FVector C)
	{
		return A[FirstComponent] * (B[SecondComponent] - C[SecondComponent]) + B[FirstComponent] * (C[SecondComponent] - A[SecondComponent]) + C[FirstComponent] * (A[SecondComponent] - B[SecondComponent]);
	};

	FORCEINLINE static double S_E(const int FirstComponent, const int SecondComponent, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const double RA, const double RB, const double RC, const double RD, const double UVW)
	{
		return (RA * S_D(FirstComponent, SecondComponent, B, C, D) - RB * S_D(FirstComponent, SecondComponent, C, D, A) + RC * S_D(FirstComponent, SecondComponent, D, A, B) - RD * S_D(FirstComponent, SecondComponent, A, B, C)) / UVW;
	};

	static double S_SQ(const FVector& P)
	{
		return P.X * P.X + P.Y * P.Y + P.Z * P.Z;
	};


	PCGEXCORE_API bool FindSphereFrom4Points(const FVector& A, const FVector& B, const FVector& C, const FVector& D, FSphere& OutSphere);
	PCGEXCORE_API bool FindSphereFrom4Points(const TArrayView<FVector>& Positions, const int32 (&Vtx)[4], FSphere& OutSphere);
	PCGEXCORE_API void GetCircumcenter(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], FVector& OutCircumcenter);

	/** Compute 2D circumcenter (using only X,Y) with Z averaged from input vertices */
	PCGEXCORE_API void GetCircumcenter2D(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], FVector& OutCircumcenter);

	PCGEXCORE_API void GetCentroid(const TArrayView<FVector>& Positions, const int32 (&Vtx)[4], FVector& OutCentroid);
	PCGEXCORE_API void GetCentroid(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], FVector& OutCentroid);
	PCGEXCORE_API void GetLongestEdge(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], uint64& Edge);
	PCGEXCORE_API void GetLongestEdge(const TArrayView<FVector>& Positions, const int32 (&Vtx)[4], uint64& Edge);

	PCGEXCORE_API FVector GetBarycentricCoordinates(const FVector& Point, const FVector& A, const FVector& B, const FVector& C);
	PCGEXCORE_API bool IsPointInTriangle(const FVector& P, const FVector& A, const FVector& B, const FVector& C);

	/**
		 *	 Leave <---.Apex-----> Arrive (Direction)
		 *		   . '   |    '  .  
		 *		A----Anchor---------B
		 */
	struct PCGEXCORE_API FApex
	{
		FApex()
		{
		}

		FApex(const FVector& Start, const FVector& End, const FVector& InApex);

		FVector Direction = FVector::ZeroVector;
		FVector Anchor = FVector::ZeroVector;
		FVector TowardStart = FVector::ZeroVector;
		FVector TowardEnd = FVector::ZeroVector;
		double Alpha = 0;

		FVector GetAnchorNormal(const FVector& Location) const
		{
			return (Anchor - Location).GetSafeNormal();
		}

		void Scale(const double InScale);
		void Extend(const double InSize);

		static FApex FromStartOnly(const FVector& Start, const FVector& InApex)
		{
			return FApex(Start, InApex, InApex);
		}

		static FApex FromEndOnly(const FVector& End, const FVector& InApex)
		{
			return FApex(InApex, End, InApex);
		}
	};

	struct PCGEXCORE_API FExCenterArc
	{
		double Radius = 0;
		double Theta = 0;

		FVector Center = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		FVector Hand = FVector::ZeroVector;
		FVector OtherHand = FVector::ZeroVector;

		// When set, the arc parametrization is degenerate (GetLocationOnArc/GetLength are not meaningful);
		// callers must handle the straight-line case themselves.
		bool bIsLine = false;

		FExCenterArc()
		{
		}


		/**
			 * ExCenter arc from 3 points.
			 * The arc center will be opposite to B
			 * @param A 
			 * @param B 
			 * @param C 
			 */
		FExCenterArc(const FVector& A, const FVector& B, const FVector& C);

		/**
			 * ExCenter arc from 2 segments.
			 * The arc center will be opposite to B
			 * @param A1 
			 * @param B1 
			 * @param A2 
			 * @param B2
			 * @param MaxLength 
			 */
		FExCenterArc(const FVector& A1, const FVector& B1, const FVector& A2, const FVector& B2, const double MaxLength = 100000);

		/**
		 * Arc tangent to TangentDir at A, passing through C.
		 * Produces the unique seamless arc that leaves A along +TangentDir and reaches C; both A and C lie exactly on it.
		 * Robust for any C off the tangent line (no synthetic corner needed); sets bIsLine when C is near-collinear
		 * with TangentDir or TangentDir is degenerate (zero-length).
		 * @param A Start point, lies on the arc (Alpha 0)
		 * @param TangentDir Direction the arc departs A along. Need not be normalized, but its SIGN matters:
		 *                   flipping it yields the complementary arc (the sweep going the other way around the circle).
		 * @param C End point, lies on the arc (Alpha 1)
		 */
		static FExCenterArc MakeTangent(const FVector& A, const FVector& TangentDir, const FVector& C);

		FORCEINLINE double GetLength() const
		{
			return Radius * Theta;
		}

		/**
			 * 
			 * @param Alpha 0-1 normalized range on the arc
			 * @return 
			 */
		FVector GetLocationOnArc(const double Alpha) const;
	};

	PCGEXCORE_API bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);
	PCGEXCORE_API bool IsPointInPolygon(const FVector& Point, const TArray<FVector2D>& Polygon);

	PCGEXCORE_API bool IsAnyPointInPolygon(const TArray<FVector2D>& Points, const TArray<FVector2D>& Polygon);

	/**
	 * Squared distance from a 2D point to the segment AB. Tighter than promoting
	 * to FVector and calling FMath::PointDistToSegmentSquared (which is 3D-only).
	 */
	FORCEINLINE static float DistancePointToSegmentSquared2D(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const float L2 = static_cast<float>(AB.SquaredLength());
		if (L2 <= UE_SMALL_NUMBER)
		{
			return static_cast<float>((P - A).SquaredLength());
		}
		const float T = FMath::Clamp(static_cast<float>(FVector2D::DotProduct(P - A, AB) / L2), 0.0f, 1.0f);
		const FVector2D Closest = A + AB * T;
		return static_cast<float>((P - Closest).SquaredLength());
	}

	/**
	 * Signed distance from a pre-projected local-space point to an extruded polygon
	 * prism (outline in local XY, extruded along local Z by [ZMin, ZMax]).
	 * Negative inside, positive outside, zero on surface.
	 * LocalPoint must already be in the prism's projection frame -- the caller handles
	 * the WorldOrigin offset and quaternion unrotation before calling.
	 * Returns +MAX_float for degenerate outlines (< 3 vertices).
	 */
	PCGEXCORE_API float SignedDistanceToPolygonPrism(
		const FVector& LocalPoint,
		TConstArrayView<FVector2D> Outline,
		float ZMin, float ZMax);

	/**
	 * World-space AABB of an extruded prism: a 2D outline (in projection-frame XY)
	 * extruded along the projection-frame Z by [ZMin, ZMax], placed in world via
	 * (WorldOrigin + ProjectionQuat). Computes the prism's local-space AABB-of-AABB
	 * (8 corners of the outline's 2D bounds × ZMin/ZMax), rotates them through the
	 * quat, and unions. Returns an invalid box for degenerate inputs (Outline < 3
	 * verts or ZMax <= ZMin).
	 */
	PCGEXCORE_API FBox ProjectPrismToWorldAABB(
		TConstArrayView<FVector2D> Outline,
		float ZMin, float ZMax,
		const FVector& WorldOrigin,
		const FQuat& ProjectionQuat);

	/**
	 * 2D polygon-vs-polygon overlap. Concave allowed on either side; convex
	 * inputs work transparently. Reports true if any vertex of one polygon
	 * lies inside the other or any edge of one crosses any edge of the other.
	 * O(N*M).
	 */
	PCGEXCORE_API bool PolygonsOverlap2D(
		TConstArrayView<FVector2D> A,
		TConstArrayView<FVector2D> B);

	/**
	 * Project an OBB's 8 corners into a target frame. Outputs the convex hull
	 * (CCW, up to 8 verts) of the projected XY shadow plus the local Z range
	 * spanned by the corners.
	 */
	PCGEXCORE_API void ProjectOBBToFrame(
		const OBB::FOBB& OBB,
		const FVector& TargetWorldOrigin,
		const FQuat& TargetProjectionQuat,
		TArray<FVector2D, TInlineAllocator<8>>& OutHull,
		float& OutLocalZMin,
		float& OutLocalZMax);

	/**
	 * Project an extruded prism (Outline x [SourceZMin, SourceZMax] in source
	 * frame) into a target frame. Outputs the source outline expressed in the
	 * target frame's XY plane plus the target-frame Z extremes spanned by both
	 * Z extrusion rings.
	 *
	 * For coplanar source/target the lo-ring outline alone is exact; for
	 * tilted prisms the outline is a conservative slice of the true tilted
	 * volume (full fidelity would require the convex hull of both rings).
	 * The Z band always reflects both rings.
	 */
	PCGEXCORE_API void ProjectPrismToFrame(
		TConstArrayView<FVector2D> SourceOutline,
		float SourceZMin, float SourceZMax,
		const FVector& SourceWorldOrigin,
		const FQuat& SourceProjectionQuat,
		const FVector& TargetWorldOrigin,
		const FQuat& TargetProjectionQuat,
		TArray<FVector2D>& OutOutline,
		float& OutLocalZMin,
		float& OutLocalZMax);

	// L1/L∞ Voronoi edge path computation

	/** Transform 2D coordinates for L1/L∞ Voronoi computation: (x,y) -> (x+y, x-y) */
	FORCEINLINE static FVector2D TransformToLInf(const FVector2D& P)
	{
		return FVector2D(P.X + P.Y, P.X - P.Y);
	}

	/** Inverse transform: (u,v) -> ((u+v)/2, (u-v)/2) */
	FORCEINLINE static FVector2D TransformFromLInf(const FVector2D& P)
	{
		return FVector2D((P.X + P.Y) * 0.5, (P.X - P.Y) * 0.5);
	}

	/**
	 * Compute the single bend point of an L∞ metric edge between two Voronoi cell centers, if any.
	 * L∞ edges are axis-aligned or 45° diagonal, with at most one bend.
	 * @param Start Start position (2D)
	 * @param End End position (2D)
	 * @param OutBend Bend position (2D), only written when the path has a bend
	 * @return true if the edge has a bend point, false if it is a straight segment
	 */
	PCGEXCORE_API bool ComputeLInfBend(const FVector2D& Start, const FVector2D& End, FVector2D& OutBend);

	/**
	 * Compute the single bend point of an L1 metric edge between two Voronoi cell centers, if any.
	 * Uses coordinate transform to leverage L∞ computation.
	 * @param Start Start position (2D)
	 * @param End End position (2D)
	 * @param OutBend Bend position (2D), only written when the path has a bend
	 * @return true if the edge has a bend point, false if it is a straight segment
	 */
	PCGEXCORE_API bool ComputeL1Bend(const FVector2D& Start, const FVector2D& End, FVector2D& OutBend);

	/**
	 * Compute the edge path between two Voronoi cell centers for L∞ metric.
	 * L∞ edges are axis-aligned or 45° diagonal, with at most one bend.
	 * @param Start Start position (2D)
	 * @param End End position (2D)
	 * @param OutPath Output path including start, optional bend point, and end
	 */
	PCGEXCORE_API void ComputeLInfEdgePath(const FVector2D& Start, const FVector2D& End, TArray<FVector2D>& OutPath);

	/**
	 * Compute the edge path between two Voronoi cell centers for L1 metric.
	 * L1 edges are axis-aligned or 45° diagonal, with at most one bend.
	 * Uses coordinate transform to leverage L∞ computation.
	 * @param Start Start position (2D)
	 * @param End End position (2D)
	 * @param OutPath Output path including start, optional bend point, and end
	 */
	PCGEXCORE_API void ComputeL1EdgePath(const FVector2D& Start, const FVector2D& End, TArray<FVector2D>& OutPath);
}
