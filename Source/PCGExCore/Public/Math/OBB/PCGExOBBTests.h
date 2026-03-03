// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOBB.h"
#include "Math/PCGExMathBounds.h"

namespace PCGExMath::OBB
{
	// Sphere rejection (hot path - uses only FBounds)
	FORCEINLINE bool SphereOverlap(const FBounds& A, const FVector& Point, float Radius)
	{
		const float Combined = A.Radius + Radius;
		return FVector::DistSquared(A.Origin, Point) <= Combined * Combined;
	}

	FORCEINLINE bool SphereOverlap(const FBounds& A, const FBounds& B)
	{
		const float Combined = A.Radius + B.Radius;
		return FVector::DistSquared(A.Origin, B.Origin) <= Combined * Combined;
	}

	FORCEINLINE bool SphereContains(const FBounds& Container, const FVector& Point, float Radius)
	{
		// Point's sphere must be fully inside container's sphere
		return FVector::Dist(Container.Origin, Point) + Radius <= Container.Radius;
	}

	// Point-in-box (needs orientation)
	FORCEINLINE bool PointInside(const FBounds& B, const FOrientation& O, const FVector& Point)
	{
		const FVector Local = O.ToLocal(Point, B.Origin);
		return FMath::Abs(Local.X) <= B.Extents.X
			&& FMath::Abs(Local.Y) <= B.Extents.Y
			&& FMath::Abs(Local.Z) <= B.Extents.Z;
	}

	FORCEINLINE bool PointInside(const FOBB& Box, const FVector& Point)
	{
		return PointInside(Box.Bounds, Box.Orientation, Point);
	}

	FORCEINLINE bool PointInsideExpanded(const FBounds& B, const FOrientation& O, const FVector& Point, float Expansion)
	{
		const FVector Local = O.ToLocal(Point, B.Origin);
		return FMath::Abs(Local.X) <= B.Extents.X + Expansion
			&& FMath::Abs(Local.Y) <= B.Extents.Y + Expansion
			&& FMath::Abs(Local.Z) <= B.Extents.Z + Expansion;
	}

	// SAT overlap test
	PCGEXCORE_API bool SATOverlap(const FOBB& A, const FOBB& B);

	// SAT penetration depth (positive = overlapping, negative = separated).
	// Returns the minimum overlap across all 15 SAT axes (Minimum Translation Vector magnitude).
	PCGEXCORE_API float SATPenetrationDepth(const FOBB& A, const FOBB& B);

	// Sphere penetration depth (positive = overlapping, negative = separated)
	FORCEINLINE float SpherePenetrationDepth(const FBounds& A, const FBounds& B)
	{
		return (A.Radius + B.Radius) - FVector::Dist(A.Origin, B.Origin);
	}

	// Signed distance to surface (negative = inside)
	FORCEINLINE float SignedDistance(const FOBB& Box, const FVector& Point)
	{
		const FVector Local = Box.ToLocal(Point);
		const FVector Q = FVector(FMath::Abs(Local.X), FMath::Abs(Local.Y), FMath::Abs(Local.Z)) - Box.Bounds.Extents;

		const float Outside = FVector(FMath::Max(Q.X, 0.0f), FMath::Max(Q.Y, 0.0f), FMath::Max(Q.Z, 0.0f)).Size();
		const float Inside = FMath::Min(FMath::Max(Q.X, FMath::Max(Q.Y, Q.Z)), 0.0f);
		return Outside + Inside;
	}

	// Closest point on OBB surface
	FORCEINLINE FVector ClosestPoint(const FOBB& Box, const FVector& Point)
	{
		const FVector Local = Box.ToLocal(Point);
		const FVector Clamped(
			FMath::Clamp(Local.X, -Box.Bounds.Extents.X, Box.Bounds.Extents.X),
			FMath::Clamp(Local.Y, -Box.Bounds.Extents.Y, Box.Bounds.Extents.Y),
			FMath::Clamp(Local.Z, -Box.Bounds.Extents.Z, Box.Bounds.Extents.Z)
		);
		return Box.ToWorld(Clamped);
	}

	// Mode-based dispatch - maps EPCGExBoxCheckMode to actual tests

	// Point test with mode
	FORCEINLINE bool TestPoint(const FOBB& Box, const FVector& Point, EPCGExBoxCheckMode Mode, float Expansion = 0.0f)
	{
		switch (Mode)
		{
		case EPCGExBoxCheckMode::Sphere:
			return SphereOverlap(Box.Bounds, Point, 0.0f);

		case EPCGExBoxCheckMode::ExpandedSphere:
			return SphereOverlap(Box.Bounds, Point, Expansion);

		case EPCGExBoxCheckMode::ExpandedBox:
			return PointInsideExpanded(Box.Bounds, Box.Orientation, Point, Expansion);

		case EPCGExBoxCheckMode::Box:
		default:
			return PointInside(Box, Point);
		}
	}

	// OBB-OBB test with mode
	FORCEINLINE bool TestOverlap(const FOBB& A, const FOBB& B, EPCGExBoxCheckMode Mode, float Expansion = 0.0f)
	{
		switch (Mode)
		{
		case EPCGExBoxCheckMode::Sphere:
			return SphereOverlap(A.Bounds, B.Bounds);

		case EPCGExBoxCheckMode::ExpandedSphere:
			{
				const float Combined = A.Bounds.Radius + B.Bounds.Radius + Expansion;
				return FVector::DistSquared(A.Bounds.Origin, B.Bounds.Origin) <= Combined * Combined;
			}

		case EPCGExBoxCheckMode::ExpandedBox:
			{
				// Expand A for test
				FOBB ExpandedA = Factory::Expanded(A, Expansion);
				if (!SphereOverlap(ExpandedA.Bounds, B.Bounds))
				{
					return false;
				}
				return SATOverlap(ExpandedA, B);
			}

		case EPCGExBoxCheckMode::Box:
		default:
			if (!SphereOverlap(A.Bounds, B.Bounds))
			{
				return false;
			}
			return SATOverlap(A, B);
		}
	}

	// Template policy interface (for advanced compile-time dispatch)
	// TODO : Refactor related classes, filters, samplers
	template <EPCGExBoxCheckMode Mode>
	struct PCGEXCORE_API TPolicy
	{
		float Expansion = 0.0f;

		TPolicy() = default;

		explicit TPolicy(float InExpansion)
			: Expansion(InExpansion)
		{
		}

		FORCEINLINE bool TestPoint(const FOBB& Box, const FVector& Point) const
		{
			return OBB::TestPoint(Box, Point, Mode, Expansion);
		}

		FORCEINLINE bool TestOverlap(const FOBB& A, const FOBB& B) const
		{
			return OBB::TestOverlap(A, B, Mode, Expansion);
		}
	};

	// Common policy aliases
	using FPolicyBox = TPolicy<EPCGExBoxCheckMode::Box>;
	using FPolicySphere = TPolicy<EPCGExBoxCheckMode::Sphere>;
	using FPolicyExpandedBox = TPolicy<EPCGExBoxCheckMode::ExpandedBox>;
	using FPolicyExpandedSphere = TPolicy<EPCGExBoxCheckMode::ExpandedSphere>;

	// Runtime policy wrapper (when mode isn't known at compile time)
	struct PCGEXCORE_API FPolicy
	{
		EPCGExBoxCheckMode Mode = EPCGExBoxCheckMode::Box;
		float Expansion = 0.0f;

		FPolicy() = default;

		explicit FPolicy(EPCGExBoxCheckMode InMode, float InExpansion = 0.0f)
			: Mode(InMode), Expansion(InExpansion)
		{
		}

		FORCEINLINE bool TestPoint(const FOBB& Box, const FVector& Point) const
		{
			return OBB::TestPoint(Box, Point, Mode, Expansion);
		}

		FORCEINLINE bool TestOverlap(const FOBB& A, const FOBB& B) const
		{
			return OBB::TestOverlap(A, B, Mode, Expansion);
		}
	};
}
