// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExMath.h"
#include "PCGExCoreMacros.h"

namespace PCGExMath
{
	double TruncateDbl(const double Value, const EPCGExTruncateMode Mode)
	{
		switch (Mode)
		{
		case EPCGExTruncateMode::Round:
			return FMath::RoundToInt(Value);
		case EPCGExTruncateMode::Ceil:
			return FMath::CeilToDouble(Value);
		case EPCGExTruncateMode::Floor:
			return FMath::FloorToDouble(Value);
		default: case EPCGExTruncateMode::None:
			return Value;
		}
	}

	FClosestPosition::FClosestPosition(const FVector& InOrigin)
		: Origin(InOrigin)
	{
	}

	FClosestPosition::FClosestPosition(const FVector& InOrigin, const FVector& InLocation)
		: bValid(true)
		  , Origin(InOrigin)
		  , Location(InLocation)
		  , DistSquared(FVector::DistSquared(InOrigin, InLocation))
	{
	}

	FClosestPosition::FClosestPosition(const FVector& InOrigin, const FVector& InLocation, const int32 InIndex)
		: bValid(true)
		  , Index(InIndex)
		  , Origin(InOrigin)
		  , Location(InLocation)
		  , DistSquared(FVector::DistSquared(InOrigin, InLocation))
	{
	}

	bool FClosestPosition::Update(const FVector& InLocation)
	{
		if (const double Dist = FVector::DistSquared(Origin, InLocation);
			Dist < DistSquared)
		{
			bValid = true;
			DistSquared = Dist;
			Location = InLocation;
			return true;
		}

		return false;
	}

	bool FClosestPosition::Update(const FVector& InLocation, const int32 InIndex)
	{
		if (Update(InLocation))
		{
			Index = InIndex;
			return true;
		}

		return false;
	}

	FSegment::FSegment(const FVector& InA, const FVector& InB, const double Expansion)
		: A(InA)
		  , B(InB)
		  , Direction((B - A).GetSafeNormal())
		  , Bounds(PCGEX_BOX_TOLERANCE_INLINE(A, B, Expansion))
	{
	}

	bool FSegment::FindIntersection(const FVector& A2, const FVector& B2, const double SquaredTolerance, FVector& OutSelf, FVector& OutOther, const uint8 Strictness) const
	{
		// Find the closest point pair between two line segments.
		// Strictness flags exclude intersections at specific endpoints (bitmask),
		// e.g. to prevent detecting shared vertices as intersections in a mesh.
		FMath::SegmentDistToSegment(A, B, A2, B2, OutSelf, OutOther);

		if ((Strictness & static_cast<uint8>(EPCGExIntersectionStrictness::MainA)) && A == OutSelf)
		{
			return false;
		}
		if ((Strictness & static_cast<uint8>(EPCGExIntersectionStrictness::MainB)) && B == OutSelf)
		{
			return false;
		}
		if ((Strictness & static_cast<uint8>(EPCGExIntersectionStrictness::OtherA)) && A2 == OutOther)
		{
			return false;
		}
		if ((Strictness & static_cast<uint8>(EPCGExIntersectionStrictness::OtherB)) && B2 == OutOther)
		{
			return false;
		}

		if (FVector::DistSquared(OutSelf, OutOther) >= SquaredTolerance)
		{
			return false;
		}
		return true;
	}

	bool FSegment::FindIntersection(const FSegment& S, const double SquaredTolerance, FVector& OutSelf, FVector& OutOther, const uint8 Strictness) const
	{
		return FindIntersection(S.A, S.B, SquaredTolerance, OutSelf, OutOther, Strictness);
	}

	FVector SafeLinePlaneIntersection(const FVector& Pt1, const FVector& Pt2, const FVector& PlaneOrigin, const FVector& PlaneNormal, bool& bIntersect)
	{
		if (FMath::IsNearlyZero(FVector::DotProduct((Pt1 - Pt2).GetSafeNormal(), PlaneNormal)))
		{
			bIntersect = false;
			return FVector::ZeroVector;
		}

		bIntersect = true;
		return FMath::LinePlaneIntersection(Pt1, Pt2, PlaneOrigin, PlaneNormal);
	}

	bool SphereOverlap(const FSphere& S1, const FSphere& S2, double& OutOverlap)
	{
		OutOverlap = (S1.W + S2.W) - FVector::Dist(S1.Center, S2.Center);
		return OutOverlap > 0;
	}

	bool SphereOverlap(const FBoxSphereBounds& S1, const FBoxSphereBounds& S2, double& OutOverlap)
	{
		return SphereOverlap(S1.GetSphere(), S2.GetSphere(), OutOverlap);
	}

	void CheckConvex(const FVector& A, const FVector& B, const FVector& C, bool& bIsConvex, int32& OutSign, const FVector& UpVector)
	{
		// Incremental convexity test for a polygon. Call for each consecutive triple (A,B,C).
		// Computes the cross product of edges AB and AC, projected onto UpVector.
		// If the sign ever flips from a previously observed sign, the polygon is concave.
		// OutSign tracks the first non-zero sign seen; subsequent sign changes set bIsConvex=false.
		if (!bIsConvex)
		{
			return;
		}

		if (A == C)
		{
			bIsConvex = false;
			return;
		}

		const double DP = FVector::DotProduct(FVector::CrossProduct((A - B), (C - A)), UpVector);
		const int32 CurrentSign = (DP > 0.0f) ? 1 : (DP < 0.0f) ? -1 : 0;

		if (CurrentSign != 0)
		{
			if (OutSign == 0)
			{
				OutSign = CurrentSign;
			}
			else if (OutSign != CurrentSign)
			{
				bIsConvex = false;
			}
		}
	}

	bool IsDirectionWithinTolerance(const FVector& A, const FVector& B, const FRotator& Limits)
	{
		const FRotator RA = A.Rotation();
		const FRotator RB = B.Rotation();

		return FMath::Abs(FRotator::NormalizeAxis(RA.Yaw - RB.Yaw)) <= Limits.Yaw && FMath::Abs(FRotator::NormalizeAxis(RA.Pitch - RB.Pitch)) <= Limits.Pitch && FMath::Abs(FRotator::NormalizeAxis(RA.Roll - RB.Roll)) <= Limits.Roll;
	}

	double GetPerpendicularDistance(const FVector& A, const FVector& B, const FVector& C)
	{
		const FVector AB = B - A;
		return FVector::Dist(C, A + FMath::Clamp(FVector::DotProduct((C - A), AB) / AB.SizeSquared(), 0, 1) * AB);
	}
}
