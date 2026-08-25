// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExBestFitPlane.h"

#include "CoreMinimal.h"
#include "MinVolumeBox3.h"
#include "Async/ParallelFor.h"
#include "Math/PCGExMathAxis.h"
#include "Utils/PCGValueRange.h"

namespace PCGExMath
{
	namespace BestFitPlaneInternal
	{
		// Eigen-solve a symmetric 3x3 covariance into variance-ordered, right-handed axes (power iteration)
		void SolveAxesFromCovariance(const double Cov[6], FVector OutAxis[3], int32 OutSwizzle[3])
		{
			// Find largest eigenvalue/eigenvector using power iteration (primary axis)
			// Try different starting vectors in case one aligns with a zero-variance direction
			const FVector StartVectors[3] = {FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1)};
			FVector V0 = StartVectors[0];

			for (int32 StartIdx = 0; StartIdx < 3; StartIdx++)
			{
				V0 = StartVectors[StartIdx];
				for (int32 Iter = 0; Iter < 8; Iter++)
				{
					V0 = FVector(
						Cov[0] * V0.X + Cov[3] * V0.Y + Cov[4] * V0.Z,
						Cov[3] * V0.X + Cov[1] * V0.Y + Cov[5] * V0.Z,
						Cov[4] * V0.X + Cov[5] * V0.Y + Cov[2] * V0.Z
						);
					// Normalize every 4th iteration to prevent numerical overflow while reducing sqrt calls
					if ((Iter & 3) == 3)
					{
						V0 = V0.GetSafeNormal();
					}
				}
				V0 = V0.GetSafeNormal();

				// If we found a non-zero eigenvector, we're done
				if (V0.SizeSquared() > KINDA_SMALL_NUMBER)
				{
					break;
				}
			}

			// Find second eigenvector perpendicular to first
			FVector V1 = FVector::CrossProduct(V0, FVector::UpVector);
			if (V1.SizeSquared() < KINDA_SMALL_NUMBER)
			{
				V1 = FVector::CrossProduct(V0, FVector::ForwardVector);
			}
			V1 = V1.GetSafeNormal();

			// Store initial perpendicular vector - needed if this direction has zero variance
			const FVector V1Initial = V1;

			for (int32 Iter = 0; Iter < 8; Iter++)
			{
				FVector Temp(
					Cov[0] * V1.X + Cov[3] * V1.Y + Cov[4] * V1.Z,
					Cov[3] * V1.X + Cov[1] * V1.Y + Cov[5] * V1.Z,
					Cov[4] * V1.X + Cov[5] * V1.Y + Cov[2] * V1.Z
					);
				Temp = Temp - FVector::DotProduct(Temp, V0) * V0; // Gram-Schmidt

				// If covariance sends V1 to near-zero, this direction has zero variance
				// Keep the initial perpendicular vector as it's already the correct eigenvector
				if (Temp.SizeSquared() < KINDA_SMALL_NUMBER)
				{
					V1 = V1Initial;
					break;
				}

				V1 = Temp;
				// Normalize every 4th iteration
				if ((Iter & 3) == 3)
				{
					V1 = V1.GetSafeNormal();
				}
			}
			V1 = V1.GetSafeNormal(); // Final normalization

			// Third axis is cross product of two unit vectors - result is already unit length
			FVector V2 = FVector::CrossProduct(V0, V1);

			// Compute variance (eigenvalue approximation) for each axis
			double Variance[3];
			for (int32 AxisIdx = 0; AxisIdx < 3; AxisIdx++)
			{
				const FVector& V = (AxisIdx == 0) ? V0 : (AxisIdx == 1) ? V1 : V2;
				const FVector Temp(
					Cov[0] * V.X + Cov[3] * V.Y + Cov[4] * V.Z,
					Cov[3] * V.X + Cov[1] * V.Y + Cov[5] * V.Z,
					Cov[4] * V.X + Cov[5] * V.Y + Cov[2] * V.Z
					);
				Variance[AxisIdx] = FVector::DotProduct(V, Temp);
			}

			// Sort axes by variance (largest to smallest) with index tie-breaker for determinism
			OutSwizzle[0] = 0;
			OutSwizzle[1] = 1;
			OutSwizzle[2] = 2;
			Algo::Sort(TArrayView<int32>(OutSwizzle, 3), [&](const int32 A, const int32 B)
			{
				if (Variance[A] != Variance[B])
				{
					return Variance[A] > Variance[B];
				}
				return A < B; // Tie-breaker by index for determinism
			});

			FVector TempAxes[3] = {V0, V1, V2};
			FVector X = TempAxes[OutSwizzle[0]]; // Largest variance
			FVector Y = TempAxes[OutSwizzle[1]]; // Medium variance

			// Ensure right-handed system - cross product of unit vectors is unit length
			FVector Z = FVector::CrossProduct(X, Y);
			Y = FVector::CrossProduct(Z, X);

			// Make Z point upward
			if (FVector::DotProduct(Z, FVector::UpVector) < 0)
			{
				Z *= -1;
			}

			// X, Y, Z are already normalized from cross products of unit vectors
			OutAxis[0] = X;
			OutAxis[1] = Y;
			OutAxis[2] = Z;
		}

		// Normalized covariance of points around a given centroid (symmetric 3x3)
		template <typename PointAccessor>
		void BuildCovariance(const int32 NumPoints, PointAccessor&& GetPoint, const FVector& InCentroid, double OutCov[6])
		{
			for (int32 i = 0; i < 6; i++)
			{
				OutCov[i] = 0; // XX, YY, ZZ, XY, XZ, YZ
			}
			for (int32 i = 0; i < NumPoints; i++)
			{
				const FVector P = GetPoint(i) - InCentroid;
				OutCov[0] += P.X * P.X; // XX
				OutCov[1] += P.Y * P.Y; // YY
				OutCov[2] += P.Z * P.Z; // ZZ
				OutCov[3] += P.X * P.Y; // XY
				OutCov[4] += P.X * P.Z; // XZ
				OutCov[5] += P.Y * P.Z; // YZ
			}
			const double Scale = 1.0 / NumPoints;
			for (int32 i = 0; i < 6; i++)
			{
				OutCov[i] *= Scale;
			}
		}

		// Fast PCA-based best fit plane computation using covariance matrix
		template <typename PointAccessor>
		void ComputePCA(const int32 NumPoints, PointAccessor&& GetPoint, FVector& OutCentroid, FVector OutAxis[3], FVector& OutExtents, int32 OutSwizzle[3])
		{
			// Compute centroid
			OutCentroid = FVector::ZeroVector;
			for (int32 i = 0; i < NumPoints; i++)
			{
				OutCentroid += GetPoint(i);
			}
			OutCentroid /= NumPoints;

			double Cov[6];
			BuildCovariance(NumPoints, GetPoint, OutCentroid, Cov);
			SolveAxesFromCovariance(Cov, OutAxis, OutSwizzle);

			// Compute extents by projecting all points onto axes
			FVector MinProj(TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			FVector MaxProj(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());

			for (int32 i = 0; i < NumPoints; i++)
			{
				const FVector P = GetPoint(i) - OutCentroid;
				for (int32 AxisIdx = 0; AxisIdx < 3; AxisIdx++)
				{
					const double Proj = FVector::DotProduct(P, OutAxis[AxisIdx]);
					MinProj[AxisIdx] = FMath::Min(MinProj[AxisIdx], Proj);
					MaxProj[AxisIdx] = FMath::Max(MaxProj[AxisIdx], Proj);
				}
			}

			OutExtents = (MaxProj - MinProj) * 0.5;
		}
	}

	FBestFitPlane::FBestFitPlane(const TConstPCGValueRange<FTransform>& InTransforms, const bool bUsePreciseBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::FBestFitPlane);

		if (bUsePreciseBounds)
		{
			UE::Geometry::FOrientedBox3d OrientedBox{};
			UE::Geometry::TMinVolumeBox3<double> Box;

			Centroid = FVector::ZeroVector;

			Box.Solve(InTransforms.Num(), [&](int32 i)
			{
				const FVector P = InTransforms[i].GetLocation();
				Centroid += P;
				return P;
			});

			Centroid /= InTransforms.Num();

			if (Box.IsSolutionAvailable())
			{
				Box.GetResult(OrientedBox);
				ProcessBox(OrientedBox);
			}
		}
		else
		{
			BestFitPlaneInternal::ComputePCA(
				InTransforms.Num(),
				[&](int32 i)
				{
					return InTransforms[i].GetLocation();
				},
				Centroid, Axis, Extents, Swizzle
				);
		}
	}

	FBestFitPlane::FBestFitPlane(const TConstPCGValueRange<FTransform>& InTransforms, const TArrayView<int32> InIndices, const bool bUsePreciseBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::FBestFitPlane);

		if (bUsePreciseBounds)
		{
			UE::Geometry::FOrientedBox3d OrientedBox{};
			UE::Geometry::TMinVolumeBox3<double> Box;

			Centroid = FVector::ZeroVector;

			Box.Solve(InIndices.Num(), [&](int32 i)
			{
				const FVector P = InTransforms[InIndices[i]].GetLocation();
				Centroid += P;
				return P;
			});

			Centroid /= InIndices.Num();

			if (Box.IsSolutionAvailable())
			{
				Box.GetResult(OrientedBox);
				ProcessBox(OrientedBox);
			}
		}
		else
		{
			BestFitPlaneInternal::ComputePCA(
				InIndices.Num(),
				[&](int32 i)
				{
					return InTransforms[InIndices[i]].GetLocation();
				},
				Centroid, Axis, Extents, Swizzle
				);
		}
	}

	FBestFitPlane::FBestFitPlane(const TArrayView<const FVector> InPositions, const bool bUsePreciseBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::FBestFitPlane);

		if (bUsePreciseBounds)
		{
			UE::Geometry::FOrientedBox3d OrientedBox{};
			UE::Geometry::TMinVolumeBox3<double> Box;

			Centroid = FVector::ZeroVector;

			Box.Solve(InPositions.Num(), [&](int32 i)
			{
				const FVector P = InPositions[i];
				Centroid += P;
				return P;
			});

			Centroid /= InPositions.Num();

			if (Box.IsSolutionAvailable())
			{
				Box.GetResult(OrientedBox);
				ProcessBox(OrientedBox);
			}
		}
		else
		{
			BestFitPlaneInternal::ComputePCA(
				InPositions.Num(),
				[&](int32 i)
				{
					return InPositions[i];
				},
				Centroid, Axis, Extents, Swizzle
				);
		}
	}

	FBestFitPlane::FBestFitPlane(const TArrayView<const FVector2D> InPositions, const bool bUsePreciseBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::FBestFitPlane);

		if (bUsePreciseBounds)
		{
			UE::Geometry::FOrientedBox3d OrientedBox{};
			UE::Geometry::TMinVolumeBox3<double> Box;

			Centroid = FVector::ZeroVector;

			Box.Solve(InPositions.Num(), [&](int32 i)
			{
				const FVector P = FVector(InPositions[i], 0);
				Centroid += P;
				return P;
			});

			Centroid /= InPositions.Num();

			if (Box.IsSolutionAvailable())
			{
				Box.GetResult(OrientedBox);
				ProcessBox(OrientedBox);
			}
		}
		else
		{
			BestFitPlaneInternal::ComputePCA(
				InPositions.Num(),
				[&](int32 i)
				{
					return FVector(InPositions[i], 0);
				},
				Centroid, Axis, Extents, Swizzle
				);
		}
	}

	FBestFitPlane::FBestFitPlane(const int32 NumElements, FGetElementPositionCallback&& GetPointFunc, const bool bUsePreciseBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::FBestFitPlane);

		if (bUsePreciseBounds)
		{
			UE::Geometry::FOrientedBox3d OrientedBox{};
			UE::Geometry::TMinVolumeBox3<double> Box;

			Centroid = FVector::ZeroVector;

			Box.Solve(NumElements, [&](int32 i)
			{
				const FVector P = GetPointFunc(i);
				Centroid += P;
				return P;
			});

			Centroid /= NumElements;

			if (Box.IsSolutionAvailable())
			{
				Box.GetResult(OrientedBox);
				ProcessBox(OrientedBox);
			}
		}
		else
		{
			BestFitPlaneInternal::ComputePCA(
				NumElements,
				[&](int32 i)
				{
					return GetPointFunc(i);
				},
				Centroid, Axis, Extents, Swizzle
				);
		}
	}

	FBestFitPlane::FBestFitPlane(const int32 NumElements, FGetElementPositionCallback&& GetPointFunc, const FVector& Extra, const bool bUsePreciseBounds)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::FBestFitPlane);

		if (bUsePreciseBounds)
		{
			UE::Geometry::FOrientedBox3d OrientedBox{};
			UE::Geometry::TMinVolumeBox3<double> Box;

			Centroid = FVector::ZeroVector;

			Box.Solve(NumElements + 1, [&](int32 i)
			{
				const FVector P = i == NumElements ? Extra : GetPointFunc(i);
				Centroid += P;
				return P;
			});

			Centroid /= NumElements + 1;

			if (Box.IsSolutionAvailable())
			{
				Box.GetResult(OrientedBox);
				ProcessBox(OrientedBox);
			}
		}
		else
		{
			BestFitPlaneInternal::ComputePCA(
				NumElements + 1,
				[&](int32 i)
				{
					return i == NumElements ? Extra : GetPointFunc(i);
				},
				Centroid, Axis, Extents, Swizzle
				);
		}
	}

	FBestFitPlane FBestFitPlane::PlaneOnly(const int32 NumElements, FGetElementPositionCallback&& GetPointFunc, const FVector& InKnownCentroid)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FBestFitPlane::PlaneOnly);

		FBestFitPlane Result;
		Result.Centroid = InKnownCentroid;

		double Cov[6];
		BestFitPlaneInternal::BuildCovariance(NumElements, GetPointFunc, InKnownCentroid, Cov);
		BestFitPlaneInternal::SolveAxesFromCovariance(Cov, Result.Axis, Result.Swizzle);

		return Result;
	}

	FTransform FBestFitPlane::GetTransform() const
	{
		FTransform Transform = FTransform(FMatrix(Axis[0], Axis[1], Axis[2], FVector::Zero()));
		Transform.SetLocation(Centroid);
		Transform.SetScale3D(FVector::OneVector);
		return Transform;
	}

	FTransform FBestFitPlane::GetTransform(const EPCGExAxisOrder Order) const
	{
		int32 Comps[3] = {0, 0, 0};
		GetAxesOrder(Order, Comps);

		FTransform Transform = FTransform(FMatrix(Axis[Comps[0]], Axis[Comps[1]], Axis[Comps[2]], FVector::Zero()));
		Transform.SetLocation(Centroid);
		Transform.SetScale3D(FVector::OneVector);
		return Transform;
	}

	FVector FBestFitPlane::GetExtents(const EPCGExAxisOrder Order) const
	{
		int32 Comps[3] = {0, 0, 0};
		GetAxesOrder(Order, Comps);

		// Reorder extents to match the axis order
		return FVector(Extents[Comps[0]], Extents[Comps[1]], Extents[Comps[2]]);
	}

	void FBestFitPlane::ProcessBox(const UE::Geometry::FOrientedBox3d& Box)
	{
		Centroid = Box.Center();

		// Sort by extents with index tie-breaker for determinism
		Algo::Sort(Swizzle, [&](const int32 A, const int32 B)
		{
			if (Box.Extents[A] != Box.Extents[B])
			{
				return Box.Extents[A] > Box.Extents[B];
			}
			return A < B; // Tie-breaker by index for determinism
		});

		Extents[0] = Box.Extents[Swizzle[0]];
		Extents[1] = Box.Extents[Swizzle[1]];
		Extents[2] = Box.Extents[Swizzle[2]];

		// Pick raw axes (already normalized from frame)
		FVector X = Box.Frame.GetAxis(Swizzle[0]); // Longest
		FVector Y = Box.Frame.GetAxis(Swizzle[1]); // Median

		// Re-orthogonalize using cross product to avoid flip
		// Cross product of unit vectors is unit length
		FVector Z = FVector::CrossProduct(X, Y);
		Y = FVector::CrossProduct(Z, X);

		// Make sure Z points upward
		if (FVector::DotProduct(Z, FVector::UpVector) < 0)
		{
			Z *= -1;
		}

		// Axes are already normalized
		Axis[0] = X;
		Axis[1] = Y;
		Axis[2] = Z;
	}
}
