// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/Geo/PCGExGeo.h"

#include "CoreMinimal.h"
#include "GeomTools.h"
#include "Async/ParallelFor.h"
#include "Data/PCGExPointIO.h"
#include "Math/ConvexHull2d.h"
#include "Math/PCGExMath.h"
#include "Math/PCGExMathAxis.h"
#include "Math/OBB/PCGExOBB.h"

namespace PCGExMath::Geo
{
	bool FindSphereFrom4Points(const FVector& A, const FVector& B, const FVector& C, const FVector& D, FSphere& OutSphere)
	{
		//Shamelessly stolen from https://stackoverflow.com/questions/37449046/how-to-calculate-the-sphere-center-with-4-points

		const double U = S_U(A, B, C, D, B, C, D, A);
		const double V = S_U(C, D, A, B, D, A, B, C);
		const double W = S_U(A, C, D, B, B, D, A, C);
		const double UVW = 2 * (U + V + W);

		if (UVW == 0.0)
		{
			return false;
		} // Coplanar

		constexpr int C_X = 0;
		constexpr int C_Y = 1;
		constexpr int C_Z = 2;
		const double RA = S_SQ(A);
		const double RB = S_SQ(B);
		const double RC = S_SQ(C);
		const double RD = S_SQ(D);

		const FVector Center = FVector(
			S_E(C_Y, C_Z, A, B, C, D, RA, RB, RC, RD, UVW),
			S_E(C_Z, C_X, A, B, C, D, RA, RB, RC, RD, UVW),
			S_E(C_X, C_Y, A, B, C, D, RA, RB, RC, RD, UVW));

		const double radius = FMath::Sqrt(S_SQ(FVector(A - Center)));

		OutSphere = FSphere(Center, radius);
		return true;
	}

	bool FindSphereFrom4Points(const TArrayView<FVector>& Positions, const int32 (&Vtx)[4], FSphere& OutSphere)
	{
		return FindSphereFrom4Points(Positions[Vtx[0]], Positions[Vtx[1]], Positions[Vtx[2]], Positions[Vtx[3]], OutSphere);
	}

	void GetCircumcenter(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], FVector& OutCircumcenter)
	{
		// Calculate midpoints of two sides
		const FVector& A = Positions[Vtx[0]];
		const FVector& B = Positions[Vtx[1]];
		const FVector& C = Positions[Vtx[2]];


		const FVector AC = C - A;
		const FVector AB = B - A;
		const FVector ABxAC = AB.Cross(AC);

		// this is the vector from a TO the circumsphere center
		const FVector ToCircumsphereCenter = (ABxAC.Cross(AB) * AC.SquaredLength() + AC.Cross(ABxAC) * AB.SquaredLength()) / (2 * ABxAC.SquaredLength());
		//float Radius = ToCircumsphereCenter.Length();

		// The 3 space coords of the circumsphere center then:
		OutCircumcenter = A + ToCircumsphereCenter;
	}

	void GetCircumcenter2D(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], FVector& OutCircumcenter)
	{
		// Compute 2D circumcenter using only X,Y coordinates
		const FVector& A = Positions[Vtx[0]];
		const FVector& B = Positions[Vtx[1]];
		const FVector& C = Positions[Vtx[2]];

		// 2D circumcenter formula
		const double Ax = A.X, Ay = A.Y;
		const double Bx = B.X, By = B.Y;
		const double Cx = C.X, Cy = C.Y;

		const double D = 2.0 * (Ax * (By - Cy) + Bx * (Cy - Ay) + Cx * (Ay - By));

		if (FMath::IsNearlyZero(D))
		{
			// Degenerate case - collinear points, use centroid
			OutCircumcenter = (A + B + C) / 3.0;
			return;
		}

		const double Asq = Ax * Ax + Ay * Ay;
		const double Bsq = Bx * Bx + By * By;
		const double Csq = Cx * Cx + Cy * Cy;

		const double Ux = (Asq * (By - Cy) + Bsq * (Cy - Ay) + Csq * (Ay - By)) / D;
		const double Uy = (Asq * (Cx - Bx) + Bsq * (Ax - Cx) + Csq * (Bx - Ax)) / D;

		// For 2D Voronoi on a projection plane, the circumcenter lies ON the plane
		// In projected space, points on the plane have a constant Z value
		// Use the average Z which represents the plane's depth from origin
		// For perfectly coplanar points, all Z values are equal so average = that value
		const double Uz = (A.Z + B.Z + C.Z) / 3.0;

		OutCircumcenter = FVector(Ux, Uy, Uz);
	}

	void GetCentroid(const TArrayView<FVector>& Positions, const int32 (&Vtx)[4], FVector& OutCentroid)
	{
		OutCentroid = FVector::ZeroVector;
		for (int i = 0; i < 4; i++)
		{
			OutCentroid += Positions[Vtx[i]];
		}
		OutCentroid /= 4;
	}

	void GetCentroid(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], FVector& OutCentroid)
	{
		OutCentroid = FVector::ZeroVector;
		for (int i = 0; i < 3; i++)
		{
			OutCentroid += Positions[Vtx[i]];
		}
		OutCentroid /= 3;
	}

	void GetLongestEdge(const TArrayView<FVector>& Positions, const int32 (&Vtx)[3], uint64& Edge)
	{
		double Dist = 0;
		for (int i = 0; i < 3; i++)
		{
			for (int j = i + 1; j < 3; j++)
			{
				const double LocalDist = FVector::DistSquared(Positions[Vtx[i]], Positions[Vtx[j]]);
				if (LocalDist > Dist)
				{
					Dist = LocalDist;
					Edge = PCGEx::H64U(Vtx[i], Vtx[j]);
				}
			}
		}
	}

	void GetLongestEdge(const TArrayView<FVector>& Positions, const int32 (&Vtx)[4], uint64& Edge)
	{
		double Dist = 0;
		for (int i = 0; i < 4; i++)
		{
			for (int j = i + 1; j < 4; j++)
			{
				const double LocalDist = FVector::DistSquared(Positions[Vtx[i]], Positions[Vtx[j]]);
				if (LocalDist > Dist)
				{
					Dist = LocalDist;
					Edge = PCGEx::H64U(Vtx[i], Vtx[j]);
				}
			}
		}
	}

	FVector GetBarycentricCoordinates(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector AB = B - A;
		const FVector AC = C - A;
		const FVector AD = Point - A;

		const double d00 = FVector::DotProduct(AB, AB);
		const double d01 = FVector::DotProduct(AB, AC);
		const double d11 = FVector::DotProduct(AC, AC);
		const double d20 = FVector::DotProduct(AD, AB);
		const double d21 = FVector::DotProduct(AD, AC);

		const double Den = d00 * d11 - d01 * d01;
		const double V = (d11 * d20 - d01 * d21) / Den;
		const double W = (d00 * d21 - d01 * d20) / Den;
		const double U = 1.0f - V - W;

		return FVector(U, V, W);
	}

	bool IsPointInTriangle(const FVector& P, const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector& D = FVector::CrossProduct(B - A, P - A);
		return (FVector::DotProduct(D, FVector::CrossProduct(C - B, P - B)) >= 0) && (FVector::DotProduct(D, FVector::CrossProduct(A - C, P - C)) >= 0);
	}

	FApex::FApex(const FVector& Start, const FVector& End, const FVector& InApex)
	{
		Direction = (Start - End).GetSafeNormal();
		Anchor = FMath::ClosestPointOnSegment(InApex, Start, End);

		const double DistToStart = FVector::Dist(Start, Anchor);
		const double DistToEnd = FVector::Dist(End, Anchor);
		TowardStart = Direction * (DistToStart * -1);
		TowardEnd = Direction * DistToEnd;
		Alpha = DistToStart / (DistToStart + DistToEnd);
	}

	void FApex::Scale(const double InScale)
	{
		TowardStart *= InScale;
		TowardEnd *= InScale;
	}

	void FApex::Extend(const double InSize)
	{
		TowardStart += Direction * InSize;
		TowardEnd += Direction * -InSize;
	}

	FExCenterArc::FExCenterArc(const FVector& A, const FVector& B, const FVector& C)
	{
		// Construct an arc from A through B to C by finding the arc center.
		// The center lies at the intersection of: (1) the perpendicular bisector plane of AB,
		// and (2) the line through C perpendicular to the BC plane.
		// Once the center is found, Hand/OtherHand define the arc's angular span.
		const FVector Up = GetNormal(A, B, C);
		bool bIntersect = true;

		Center = SafeLinePlaneIntersection(C, C + GetNormal(B, C, C + Up), A, (A - B).GetSafeNormal(), bIntersect);

		if (!bIntersect)
		{
			Center = FMath::Lerp(A, C, 0.5);
		} // Parallel lines, place center right in the middle

		Radius = FVector::Dist(C, Center);

		Hand = (A - Center).GetSafeNormal();
		OtherHand = (C - Center).GetSafeNormal();

		bIsLine = FMath::IsNearlyEqual(FMath::Abs(FVector::DotProduct(Hand, OtherHand)), 1);

		Normal = FVector::CrossProduct(Hand, OtherHand).GetSafeNormal();
		Theta = FMath::Acos(FVector::DotProduct(Hand, OtherHand));
	}

	FExCenterArc::FExCenterArc(const FVector& A1, const FVector& B1, const FVector& A2, const FVector& B2, const double MaxLength)
	{
		// Construct an arc connecting two directed edges (A1→B1 and A2→B2).
		// The center is found at the closest point between the two edge-perpendicular rays,
		// clamped to MaxLength to handle near-parallel edges gracefully.
		const FVector& N1 = GetNormal(B1, A1, A1 + GetNormal(B1, A1, A2));
		const FVector& N2 = GetNormal(B2, A2, A2 + GetNormal(B2, A2, A1));

		if (FMath::IsNearlyZero(FVector::DotProduct(N1, N2)))
		{
			Center = FMath::Lerp(B1, B2, 0.5);
		}
		else
		{
			FVector OutA = FVector::ZeroVector;
			FVector OutB = FVector::ZeroVector;
			FMath::SegmentDistToSegment(B1 + N1 * -MaxLength, B1 + N1 * MaxLength, B2 + N2 * -MaxLength, B2 + N2 * MaxLength, OutA, OutB);
			Center = FMath::Lerp(OutA, OutB, 0.5);
		}

		Radius = FVector::Dist(A2, Center);

		Hand = (B1 - Center).GetSafeNormal();
		OtherHand = (B2 - Center).GetSafeNormal();

		Normal = FVector::CrossProduct(Hand, OtherHand).GetSafeNormal();
		Theta = FMath::Acos(FVector::DotProduct(Hand, OtherHand));
	}

	FExCenterArc FExCenterArc::MakeTangent(const FVector& A, const FVector& TangentDir, const FVector& C)
	{
		// Seamless arc: tangent to TangentDir at A, passing through C.
		// The center sits on the ray from A perpendicular to TangentDir (which guarantees tangency at A),
		// at radius R = |AC|^2 / (2b), where b is AC's magnitude along that perpendicular.
		// This lands both A (Alpha 0) and C (Alpha 1) exactly on the circle, for any C off the tangent line.
		FExCenterArc Arc;

		const FVector D = C - A;
		const FVector Tangent = TangentDir.GetSafeNormal();

		// Component of AC perpendicular to the tangent, pointing from the tangent line toward C
		FVector Perp = D - FVector::DotProduct(D, Tangent) * Tangent;
		const double B = Perp.Length();

		if (B <= UE_KINDA_SMALL_NUMBER || Tangent.IsNearlyZero())
		{
			// C is (near) collinear with the tangent, or the tangent itself is degenerate (zero-length,
			// e.g. a duplicated terminal point upstream): no curvature, degenerate to a line
			Arc.bIsLine = true;
			Arc.Center = FMath::Lerp(A, C, 0.5);
			Arc.Radius = FVector::Dist(A, C) * 0.5;
			Arc.Hand = (A - Arc.Center).GetSafeNormal();
			Arc.OtherHand = (C - Arc.Center).GetSafeNormal();
			Arc.Normal = FVector::CrossProduct(Arc.Hand, Arc.OtherHand).GetSafeNormal();
			return Arc;
		}

		Perp /= B;

		Arc.Radius = D.SizeSquared() / (2.0 * B);
		Arc.Center = A + Perp * Arc.Radius;

		Arc.Hand = (A - Arc.Center).GetSafeNormal();     // == -Perp
		Arc.OtherHand = (C - Arc.Center).GetSafeNormal();

		// Forward (seamless) rotation axis: sweeping Hand around it leaves A along +Tangent.
		Arc.Normal = FVector::CrossProduct(Tangent, Perp).GetSafeNormal();

		// Central angle = twice the angle between the chord (AC) and the tangent, taken as the forward sweep in
		// (0, 2PI). This spans the semicircle case (extrusion perpendicular to the path -> radius = half the
		// segment) and the reflex case (extrusion leaning back), which the shorter-arc slerp form could not
		// represent. Only the b<=eps early-out above (extrusion collinear with the path) is a real degeneracy.
		const double CosChordAngle = FMath::Clamp(FVector::DotProduct(D.GetSafeNormal(), Tangent), -1.0, 1.0);
		Arc.Theta = 2.0 * FMath::Acos(CosChordAngle);

		return Arc;
	}

	FVector FExCenterArc::GetLocationOnArc(const double Alpha) const
	{
		// Sweep Hand around Normal by Alpha*Theta. Robust for any sweep angle (including the Theta = PI semicircle
		// and reflex arcs), unlike the sin-based slerp which divides by SinTheta and blows up at Theta = PI.
		const FVector Dir = FQuat(Normal, Alpha * Theta).RotateVector(Hand);
		return Center + (Dir * Radius);
	}


	bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
	{
		return FGeomTools2D::IsPointInPolygon(FVector2D(Point[0], Point[1]), Polygon);
	}

	bool IsPointInPolygon(const FVector& Point, const TArray<FVector2D>& Polygon)
	{
		return FGeomTools2D::IsPointInPolygon(FVector2D(Point[0], Point[1]), Polygon);
	}

	bool IsAnyPointInPolygon(const TArray<FVector2D>& Points, const TArray<FVector2D>& Polygon)
	{
		if (Points.IsEmpty())
		{
			return false;
		}
		for (const FVector2D& P : Points)
		{
			if (FGeomTools2D::IsPointInPolygon(P, Polygon))
			{
				return true;
			}
		}
		return false;
	}

	float SignedDistanceToPolygonPrism(
		const FVector& LocalPoint,
		TConstArrayView<FVector2D> Outline,
		const float ZMin, const float ZMax)
	{
		const int32 N = Outline.Num();
		if (N < 3)
		{
			return TNumericLimits<float>::Max();
		}

		const FVector2D P2D(LocalPoint.X, LocalPoint.Y);

		// Single pass: closest-edge distance + winding-number inside test.
		// Trailing-index idiom: j wraps via the prior iteration's i, no modulo.
		float MinDistSq = TNumericLimits<float>::Max();
		int32 Winding = 0;
		for (int32 i = 0, j = N - 1; i < N; j = i, ++i)
		{
			const FVector2D& A = Outline[j];
			const FVector2D& B = Outline[i];
			MinDistSq = FMath::Min(MinDistSq, DistancePointToSegmentSquared2D(P2D, A, B));
			const float Cross = (B.X - A.X) * (P2D.Y - A.Y) - (B.Y - A.Y) * (P2D.X - A.X);
			if (A.Y <= P2D.Y && B.Y > P2D.Y && Cross > 0.0f)
			{
				++Winding;
			}
			else if (A.Y > P2D.Y && B.Y <= P2D.Y && Cross < 0.0f)
			{
				--Winding;
			}
		}

		const float DZ = FMath::Max(ZMin - LocalPoint.Z, LocalPoint.Z - ZMax);
		if (Winding == 0 && DZ > 0.0f)
		{
			return FMath::Sqrt(MinDistSq + DZ * DZ);
		}

		const float EdgeDist = FMath::Sqrt(MinDistSq);
		const float D2D = (Winding != 0) ? -EdgeDist : EdgeDist;
		if (D2D > 0.0f)
		{
			return D2D;
		}
		if (DZ > 0.0f)
		{
			return DZ;
		}
		return FMath::Max(D2D, DZ);
	}

	FBox ProjectPrismToWorldAABB(
		TConstArrayView<FVector2D> Outline,
		const float ZMin, const float ZMax,
		const FVector& WorldOrigin,
		const FQuat& ProjectionQuat)
	{
		if (Outline.Num() < 3 || ZMax <= ZMin)
		{
			return FBox(ForceInit);
		}

		FBox2D Bounds2D(ForceInit);
		for (const FVector2D& V : Outline)
		{
			Bounds2D += V;
		}

		const FVector LocalCorners[8] = {
			FVector(Bounds2D.Min.X, Bounds2D.Min.Y, ZMin),
			FVector(Bounds2D.Max.X, Bounds2D.Min.Y, ZMin),
			FVector(Bounds2D.Min.X, Bounds2D.Max.Y, ZMin),
			FVector(Bounds2D.Max.X, Bounds2D.Max.Y, ZMin),
			FVector(Bounds2D.Min.X, Bounds2D.Min.Y, ZMax),
			FVector(Bounds2D.Max.X, Bounds2D.Min.Y, ZMax),
			FVector(Bounds2D.Min.X, Bounds2D.Max.Y, ZMax),
			FVector(Bounds2D.Max.X, Bounds2D.Max.Y, ZMax),
		};

		FBox AABB(ForceInit);
		for (const FVector& Local : LocalCorners)
		{
			AABB += WorldOrigin + ProjectionQuat.RotateVector(Local);
		}
		return AABB;
	}

	bool PolygonsOverlap2D(TConstArrayView<FVector2D> A, TConstArrayView<FVector2D> B)
	{
		const int32 N = A.Num();
		const int32 M = B.Num();
		if (N < 3 || M < 3)
		{
			return false;
		}

		// Crossing-number test against an arbitrary polygon (concave or convex).
		// Inlined here so the polygon argument can be a TConstArrayView -- the
		// FGeomTools2D variant requires a TArray<>.
		auto PointInPolygon = [](const FVector2D& P, TConstArrayView<FVector2D> Poly) -> bool
		{
			const int32 K = Poly.Num();
			int32 Crossings = 0;
			for (int32 i = 0, j = K - 1; i < K; j = i++)
			{
				const FVector2D& Vi = Poly[i];
				const FVector2D& Vj = Poly[j];
				if (((Vi.Y > P.Y) != (Vj.Y > P.Y)) &&
					(P.X < (Vj.X - Vi.X) * (P.Y - Vi.Y) / (Vj.Y - Vi.Y) + Vi.X))
				{
					++Crossings;
				}
			}
			return (Crossings & 1) != 0;
		};

		// Vertex-in-other tier rejects the common nested case before any edge work.
		for (const FVector2D& V : B)
		{
			if (PointInPolygon(V, A))
			{
				return true;
			}
		}
		for (const FVector2D& V : A)
		{
			if (PointInPolygon(V, B))
			{
				return true;
			}
		}

		FVector IntersectionPt;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector A1(A[i].X, A[i].Y, 0.0);
			const FVector A2(A[(i + 1) % N].X, A[(i + 1) % N].Y, 0.0);
			for (int32 j = 0; j < M; ++j)
			{
				const FVector B1(B[j].X, B[j].Y, 0.0);
				const FVector B2(B[(j + 1) % M].X, B[(j + 1) % M].Y, 0.0);
				if (FMath::SegmentIntersection2D(A1, A2, B1, B2, IntersectionPt))
				{
					return true;
				}
			}
		}
		return false;
	}

	void ProjectOBBToFrame(
		const OBB::FOBB& OBB,
		const FVector& TargetWorldOrigin,
		const FQuat& TargetProjectionQuat,
		TArray<FVector2D, TInlineAllocator<8>>& OutHull,
		float& OutLocalZMin,
		float& OutLocalZMax)
	{
		TArray<FVector2D, TInlineAllocator<8>> Corners2D;
		OutLocalZMin = TNumericLimits<float>::Max();
		OutLocalZMax = -TNumericLimits<float>::Max();

		OBB.ForEachCorner([&](const FVector& WorldCorner)
		{
			const FVector LocalCorner = TargetProjectionQuat.UnrotateVector(WorldCorner - TargetWorldOrigin);
			Corners2D.Add(FVector2D(LocalCorner.X, LocalCorner.Y));
			OutLocalZMin = FMath::Min(OutLocalZMin, static_cast<float>(LocalCorner.Z));
			OutLocalZMax = FMath::Max(OutLocalZMax, static_cast<float>(LocalCorner.Z));
		});

		TArray<int32, TInlineAllocator<8>> HullIndices;
		ConvexHull2D::ComputeConvexHull(Corners2D, HullIndices);

		OutHull.Reset();
		for (int32 Idx : HullIndices)
		{
			OutHull.Add(Corners2D[Idx]);
		}
	}

	void ProjectPrismToFrame(
		TConstArrayView<FVector2D> SourceOutline,
		float SourceZMin, float SourceZMax,
		const FVector& SourceWorldOrigin,
		const FQuat& SourceProjectionQuat,
		const FVector& TargetWorldOrigin,
		const FQuat& TargetProjectionQuat,
		TArray<FVector2D>& OutOutline,
		float& OutLocalZMin,
		float& OutLocalZMax)
	{
		OutLocalZMin = TNumericLimits<float>::Max();
		OutLocalZMax = -TNumericLimits<float>::Max();
		OutOutline.Reset();
		OutOutline.Reserve(SourceOutline.Num());

		for (const FVector2D& V : SourceOutline)
		{
			const FVector SourceLocalLo(V.X, V.Y, SourceZMin);
			const FVector SourceLocalHi(V.X, V.Y, SourceZMax);
			const FVector WorldLo = SourceWorldOrigin + SourceProjectionQuat.RotateVector(SourceLocalLo);
			const FVector WorldHi = SourceWorldOrigin + SourceProjectionQuat.RotateVector(SourceLocalHi);

			const FVector TargetLocalLo = TargetProjectionQuat.UnrotateVector(WorldLo - TargetWorldOrigin);
			const FVector TargetLocalHi = TargetProjectionQuat.UnrotateVector(WorldHi - TargetWorldOrigin);

			OutOutline.Emplace(TargetLocalLo.X, TargetLocalLo.Y);
			// Track Z extremes from BOTH lo and hi rings (target frame may be rotated
			// relative to source, so "Lo" can have higher target-frame Z than "Hi").
			OutLocalZMin = FMath::Min(OutLocalZMin, static_cast<float>(FMath::Min(TargetLocalLo.Z, TargetLocalHi.Z)));
			OutLocalZMax = FMath::Max(OutLocalZMax, static_cast<float>(FMath::Max(TargetLocalLo.Z, TargetLocalHi.Z)));
		}
	}

	bool ComputeLInfBend(const FVector2D& Start, const FVector2D& End, FVector2D& OutBend)
	{
		// Compute an L-inf (Chebyshev) metric edge path between two Voronoi cell centers.
		// In L-inf geometry, Voronoi edges consist of axis-aligned and 45-degree diagonal
		// segments. If |dx| ~= |dy|, the path is a pure diagonal (no bend). Otherwise there
		// is exactly one bend point: an axis-aligned segment followed by a diagonal.
		// This produces the characteristic "staircase" edges of Chebyshev Voronoi diagrams.
		const double DX = End.X - Start.X;
		const double DY = End.Y - Start.Y;
		const double AbsDX = FMath::Abs(DX);
		const double AbsDY = FMath::Abs(DY);

		constexpr double Tolerance = UE_DOUBLE_KINDA_SMALL_NUMBER;

		if (FMath::IsNearlyEqual(AbsDX, AbsDY, Tolerance))
		{
			// Pure diagonal, no bend needed
			return false;
		}

		if (AbsDX > AbsDY)
		{
			// Horizontal dominant: go horizontal first, then diagonal
			// Bend point: move horizontally until remaining dx equals dy
			const double DiagDist = AbsDY;
			const double HorizDist = AbsDX - DiagDist;
			OutBend = FVector2D(Start.X + FMath::Sign(DX) * HorizDist, Start.Y);
		}
		else
		{
			// Vertical dominant: go vertical first, then diagonal
			const double DiagDist = AbsDX;
			const double VertDist = AbsDY - DiagDist;
			OutBend = FVector2D(Start.X, Start.Y + FMath::Sign(DY) * VertDist);
		}

		return true;
	}

	bool ComputeL1Bend(const FVector2D& Start, const FVector2D& End, FVector2D& OutBend)
	{
		// L1 (Manhattan) and L-inf (Chebyshev) metrics are related by a 45-degree rotation.
		// Rather than implementing Manhattan edge paths from scratch, we transform the
		// endpoints into L-inf space, compute the bend there, and transform back.
		// This exploits the mathematical duality between the two distance metrics.
		FVector2D TransformedBend;
		if (!ComputeLInfBend(TransformToLInf(Start), TransformToLInf(End), TransformedBend))
		{
			return false;
		}

		OutBend = TransformFromLInf(TransformedBend);
		return true;
	}

	void ComputeLInfEdgePath(const FVector2D& Start, const FVector2D& End, TArray<FVector2D>& OutPath)
	{
		OutPath.Reset();
		OutPath.Add(Start);

		FVector2D Bend;
		if (ComputeLInfBend(Start, End, Bend))
		{
			OutPath.Add(Bend);
		}

		OutPath.Add(End);
	}

	void ComputeL1EdgePath(const FVector2D& Start, const FVector2D& End, TArray<FVector2D>& OutPath)
	{
		OutPath.Reset();
		OutPath.Add(Start);

		FVector2D Bend;
		if (ComputeL1Bend(Start, End, Bend))
		{
			OutPath.Add(Bend);
		}

		OutPath.Add(End);
	}
}
