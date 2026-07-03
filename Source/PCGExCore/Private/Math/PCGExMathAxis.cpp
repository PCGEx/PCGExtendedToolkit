// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExMathAxis.h"

namespace PCGExMathAxis
{
	// FRotationMatrix::MakeFrom* returns a zeroed matrix when its primary axis is zero, and the
	// two-axis variants' parallel-input fallback does not cover a zero secondary axis; both yield
	// garbage quaternions when converted. These helpers degrade degenerate inputs instead:
	// two-axis -> single-axis -> identity.
	FORCEINLINE bool IsUsableAxis(const FVector& V)
	{
		return V.SizeSquared() > UE_SMALL_NUMBER;
	}

	using FSingleAxisMaker = FMatrix (*)(FVector const&);
	using FDualAxisMaker = FMatrix (*)(FVector const&, FVector const&);

	FQuat SafeMakeRot(const FVector& Axis, const FSingleAxisMaker Maker)
	{
		return IsUsableAxis(Axis) ? Maker(Axis).ToQuat() : FQuat::Identity;
	}

	FQuat SafeMakeRot(const FVector& Primary, const FVector& Secondary, const FDualAxisMaker Maker, const FSingleAxisMaker PrimaryMaker, const FSingleAxisMaker SecondaryMaker)
	{
		if (!IsUsableAxis(Primary))
		{
			return SafeMakeRot(Secondary, SecondaryMaker);
		}
		if (!IsUsableAxis(Secondary))
		{
			return PrimaryMaker(Primary).ToQuat();
		}
		return Maker(Primary, Secondary).ToQuat();
	}
}

namespace PCGExMath
{
	void GetAxesOrder(EPCGExMakeRotAxis Order, int32& A, int32& B, int32& C)
	{
		switch (Order)
		{
		case EPCGExMakeRotAxis::X:
		case EPCGExMakeRotAxis::XY:
			A = 0;
			B = 1;
			C = 2;
			break;
		case EPCGExMakeRotAxis::XZ:
			A = 0;
			B = 2;
			C = 1;
			break;
		case EPCGExMakeRotAxis::Y:
		case EPCGExMakeRotAxis::YX:
			A = 1;
			B = 0;
			C = 2;
			break;
		case EPCGExMakeRotAxis::YZ:
			A = 1;
			B = 2;
			C = 0;
			break;
		case EPCGExMakeRotAxis::Z:
		case EPCGExMakeRotAxis::ZX:
			A = 2;
			B = 0;
			C = 1;
			break;
		case EPCGExMakeRotAxis::ZY:
			A = 2;
			B = 1;
			C = 0;
			break;
		}
	}

	FQuat MakeRot(const EPCGExMakeRotAxis Order, const FVector& X, const FVector& Y, const FVector& Z)
	{
		using namespace PCGExMathAxis;

		switch (Order)
		{
		default: case EPCGExMakeRotAxis::X:
			return SafeMakeRot(X, &FRotationMatrix::MakeFromX);
		case EPCGExMakeRotAxis::XY:
			return SafeMakeRot(X, Y, &FRotationMatrix::MakeFromXY, &FRotationMatrix::MakeFromX, &FRotationMatrix::MakeFromY);
		case EPCGExMakeRotAxis::XZ:
			return SafeMakeRot(X, Z, &FRotationMatrix::MakeFromXZ, &FRotationMatrix::MakeFromX, &FRotationMatrix::MakeFromZ);
		case EPCGExMakeRotAxis::Y:
			return SafeMakeRot(Y, &FRotationMatrix::MakeFromY);
		case EPCGExMakeRotAxis::YX:
			return SafeMakeRot(Y, X, &FRotationMatrix::MakeFromYX, &FRotationMatrix::MakeFromY, &FRotationMatrix::MakeFromX);
		case EPCGExMakeRotAxis::YZ:
			return SafeMakeRot(Y, Z, &FRotationMatrix::MakeFromYZ, &FRotationMatrix::MakeFromY, &FRotationMatrix::MakeFromZ);
		case EPCGExMakeRotAxis::Z:
			return SafeMakeRot(Z, &FRotationMatrix::MakeFromZ);
		case EPCGExMakeRotAxis::ZX:
			return SafeMakeRot(Z, X, &FRotationMatrix::MakeFromZX, &FRotationMatrix::MakeFromZ, &FRotationMatrix::MakeFromX);
		case EPCGExMakeRotAxis::ZY:
			return SafeMakeRot(Z, Y, &FRotationMatrix::MakeFromZY, &FRotationMatrix::MakeFromZ, &FRotationMatrix::MakeFromY);
		}
	}

	FQuat MakeRot(const EPCGExMakeRotAxis Order, const FVector& A, const FVector& B)
	{
		// Route A/B into the primary/secondary slots of the selected construction
		switch (Order)
		{
		default:
		case EPCGExMakeRotAxis::X:
		case EPCGExMakeRotAxis::XY:
			return MakeRot(Order, A, B, FVector::ZeroVector);
		case EPCGExMakeRotAxis::XZ:
			return MakeRot(Order, A, FVector::ZeroVector, B);
		case EPCGExMakeRotAxis::Y:
		case EPCGExMakeRotAxis::YX:
			return MakeRot(Order, B, A, FVector::ZeroVector);
		case EPCGExMakeRotAxis::YZ:
			return MakeRot(Order, FVector::ZeroVector, A, B);
		case EPCGExMakeRotAxis::Z:
		case EPCGExMakeRotAxis::ZX:
			return MakeRot(Order, B, FVector::ZeroVector, A);
		case EPCGExMakeRotAxis::ZY:
			return MakeRot(Order, FVector::ZeroVector, B, A);
		}
	}

	void FindOrderMatch(const FQuat& Quat, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis, int32& X, int32& Y, int32& Z, const bool bPermute)
	{
		// For each reference axis (X/Y/ZAxis), find the quaternion's local axis best aligned with it.
		// Builds a 3x3 alignment matrix M[i][j] = |dot(QuatAxis[i], RefAxis[j])|.
		// Without permutation: each reference axis independently picks its best match (may duplicate).
		// With permutation: exhaustively checks all 6 assignments to find the highest-scoring
		// bijective mapping (no local axis used twice).
		const FVector QA[3] = {Quat.GetAxisX(), Quat.GetAxisY(), Quat.GetAxisZ()};

		double M[3][3];
		for (int i = 0; i < 3; ++i)
		{
			M[i][0] = FMath::Abs(FVector::DotProduct(QA[i], XAxis));
			M[i][1] = FMath::Abs(FVector::DotProduct(QA[i], YAxis));
			M[i][2] = FMath::Abs(FVector::DotProduct(QA[i], ZAxis));
		}

		if (!bPermute)
		{
			// For each reference axis (column), pick the best-matching local axis (row)
			int32 Best[3];
			for (int j = 0; j < 3; ++j)
			{
				Best[j] = (M[0][j] >= M[1][j] && M[0][j] >= M[2][j]) ? 0 : ((M[1][j] >= M[2][j]) ? 1 : 2);
			}
			X = Best[0];
			Y = Best[1];
			Z = Best[2];
			return;
		}

		// guaranteed permutation with constant-time deterministic resolution
		static const int32 Permutations[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};

		int32 BestScore = -1;
		const int32* BestPerm = Permutations[0];

		for (int p = 0; p < 6; ++p)
		{
			const int32* P = Permutations[p];
			const double Score = M[0][P[0]] + M[1][P[1]] + M[2][P[2]];

			if (Score > BestScore)
			{
				BestScore = Score;
				BestPerm = P;
			}
		}

		// BestPerm maps local axis -> reference axis; callers consume the inverse (reference -> local)
		for (int i = 0; i < 3; ++i)
		{
			if (BestPerm[i] == 0) { X = i; }
			else if (BestPerm[i] == 1) { Y = i; }
			else { Z = i; }
		}

		check(X >= 0 && X <= 2);
		check(Y >= 0 && Y <= 2);
		check(Z >= 0 && Z <= 2);
	}

	FVector GetDirection(const FQuat& Quat, const EPCGExAxis Dir)
	{
		switch (Dir)
		{
		default: case EPCGExAxis::Forward:
			return GetDirection<EPCGExAxis::Forward>(Quat);
		case EPCGExAxis::Backward:
			return GetDirection<EPCGExAxis::Backward>(Quat);
		case EPCGExAxis::Right:
			return GetDirection<EPCGExAxis::Right>(Quat);
		case EPCGExAxis::Left:
			return GetDirection<EPCGExAxis::Left>(Quat);
		case EPCGExAxis::Up:
			return GetDirection<EPCGExAxis::Up>(Quat);
		case EPCGExAxis::Down:
			return GetDirection<EPCGExAxis::Down>(Quat);
		}
	}

	FVector GetDirection(const EPCGExAxis Dir)
	{
		switch (Dir)
		{
		default: case EPCGExAxis::Forward:
			return PCGEX_AXIS_X;
		case EPCGExAxis::Backward:
			return PCGEX_AXIS_X_N;
		case EPCGExAxis::Right:
			return PCGEX_AXIS_Y;
		case EPCGExAxis::Left:
			return PCGEX_AXIS_Y_N;
		case EPCGExAxis::Up:
			return PCGEX_AXIS_Z;
		case EPCGExAxis::Down:
			return PCGEX_AXIS_Z_N;
		}
	}

	FTransform GetIdentity(const EPCGExAxisOrder Order)
	{
		switch (Order)
		{
		default: case EPCGExAxisOrder::XYZ:
			return FTransform(FMatrix(PCGEX_AXIS_X, PCGEX_AXIS_Y, PCGEX_AXIS_Z, FVector::Zero()));
		case EPCGExAxisOrder::YZX:
			return FTransform(FMatrix(PCGEX_AXIS_Y, PCGEX_AXIS_Z, PCGEX_AXIS_X, FVector::Zero()));
		case EPCGExAxisOrder::ZXY:
			return FTransform(FMatrix(PCGEX_AXIS_Z, PCGEX_AXIS_X, PCGEX_AXIS_Y, FVector::Zero()));
		case EPCGExAxisOrder::YXZ:
			return FTransform(FMatrix(PCGEX_AXIS_Y, PCGEX_AXIS_X, PCGEX_AXIS_Z, FVector::Zero()));
		case EPCGExAxisOrder::ZYX:
			return FTransform(FMatrix(PCGEX_AXIS_Z, PCGEX_AXIS_Y, PCGEX_AXIS_X, FVector::Zero()));
		case EPCGExAxisOrder::XZY:
			return FTransform(FMatrix(PCGEX_AXIS_X, PCGEX_AXIS_Z, PCGEX_AXIS_Y, FVector::Zero()));
		}
	}

	void Swizzle(FVector& Vector, const EPCGExAxisOrder Order)
	{
		int32 A;
		int32 B;
		int32 C;
		GetAxesOrder(Order, A, B, C);
		FVector Temp = Vector;
		Vector[0] = Temp[A];
		Vector[1] = Temp[B];
		Vector[2] = Temp[C];
	}

	void Swizzle(FVector& Vector, const int32 (&Order)[3])
	{
		FVector Temp = Vector;
		Vector[0] = Temp[Order[0]];
		Vector[1] = Temp[Order[1]];
		Vector[2] = Temp[Order[2]];
	}

	FQuat MakeDirection(const EPCGExAxis Dir, const FVector& InForward)
	{
		switch (Dir)
		{
		default: case EPCGExAxis::Forward:
			return FRotationMatrix::MakeFromX(InForward * -1).ToQuat();
		case EPCGExAxis::Backward:
			return FRotationMatrix::MakeFromX(InForward).ToQuat();
		case EPCGExAxis::Right:
			return FRotationMatrix::MakeFromY(InForward * -1).ToQuat();
		case EPCGExAxis::Left:
			return FRotationMatrix::MakeFromY(InForward).ToQuat();
		case EPCGExAxis::Up:
			return FRotationMatrix::MakeFromZ(InForward * -1).ToQuat();
		case EPCGExAxis::Down:
			return FRotationMatrix::MakeFromZ(InForward).ToQuat();
		}
	}

	FQuat MakeDirection(const EPCGExAxis Dir, const FVector& InForward, const FVector& InUp)
	{
		switch (Dir)
		{
		default: case EPCGExAxis::Forward:
			return FRotationMatrix::MakeFromXZ(InForward * -1, InUp).ToQuat();
		case EPCGExAxis::Backward:
			return FRotationMatrix::MakeFromXZ(InForward, InUp).ToQuat();
		case EPCGExAxis::Right:
			return FRotationMatrix::MakeFromYZ(InForward * -1, InUp).ToQuat();
		case EPCGExAxis::Left:
			return FRotationMatrix::MakeFromYZ(InForward, InUp).ToQuat();
		case EPCGExAxis::Up:
			return FRotationMatrix::MakeFromZY(InForward * -1, InUp).ToQuat();
		case EPCGExAxis::Down:
			return FRotationMatrix::MakeFromZY(InForward, InUp).ToQuat();
		}
	}

	FVector GetNormal(const FVector& A, const FVector& B, const FVector& C)
	{
		return FVector::CrossProduct((B - A), (C - A)).GetSafeNormal();
	}

	FVector GetNormalUp(const FVector& A, const FVector& B, const FVector& Up)
	{
		return FVector::CrossProduct((B - A), ((B + Up) - A)).GetSafeNormal();
	}

	FVector SafeCrossNormal(const FVector& Up, const FVector& Dir)
	{
		const FVector Cross = FVector::CrossProduct(Up, Dir);
		if (Cross.SizeSquared() > UE_SMALL_NUMBER)
		{
			return Cross.GetSafeNormal();
		}

		// Dir is parallel to Up: fall back to the world axis least aligned with Dir
		const FVector Alt = FMath::Abs(Dir.Z) < (1.0 - UE_KINDA_SMALL_NUMBER) ? FVector::UpVector : FVector::ForwardVector;
		return FVector::CrossProduct(Alt, Dir).GetSafeNormal();
	}

	FTransform MakeLookAtTransform(const FVector& LookAt, const FVector& LookUp, const EPCGExAxisAlign AlignAxis)
	{
		switch (AlignAxis)
		{
		case EPCGExAxisAlign::Forward:
			return FTransform(FRotationMatrix::MakeFromXZ(LookAt * -1, LookUp));
		case EPCGExAxisAlign::Backward:
			return FTransform(FRotationMatrix::MakeFromXZ(LookAt, LookUp));
		case EPCGExAxisAlign::Right:
			return FTransform(FRotationMatrix::MakeFromYZ(LookAt * -1, LookUp));
		case EPCGExAxisAlign::Left:
			return FTransform(FRotationMatrix::MakeFromYZ(LookAt, LookUp));
		case EPCGExAxisAlign::Up:
			return FTransform(FRotationMatrix::MakeFromZY(LookAt * -1, LookUp));
		case EPCGExAxisAlign::Down:
			return FTransform(FRotationMatrix::MakeFromZY(LookAt, LookUp));
		default:
			return FTransform::Identity;
		}
	}

	double GetAngle(const FVector& A, const FVector& B)
	{
		const FVector Cross = FVector::CrossProduct(A, B);
		const double Atan2 = FMath::Atan2(Cross.Size(), A.Dot(B));
		return Cross.Z < 0 ? TWO_PI - Atan2 : Atan2;
	}

	double GetRadiansBetweenVectors(const FVector& A, const FVector& B, const FVector& UpVector)
	{
		// Clamp the dot: FP rounding on (near-)unit inputs can push it just outside [-1, 1], which makes Acos return NaN 
		const double Radians = FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0));
		return FVector::CrossProduct(A, B).Z < 0 ? TWO_PI - Radians : Radians;
	}

	double GetRadiansBetweenVectors(const FVector2D& A, const FVector2D& B)
	{
		// Use atan2 for robust signed angle calculation
		// This computes the counter-clockwise angle from A to B in [0, 2π)
		const double Cross = FVector2D::CrossProduct(A, B); // A.X * B.Y - A.Y * B.X
		const double Dot = FVector2D::DotProduct(A, B);
		const double Radians = FMath::Atan2(Cross, Dot);
		return (Radians >= 0) ? Radians : (Radians + TWO_PI);
	}

	double GetDegreesBetweenVectors(const FVector& A, const FVector& B, const FVector& UpVector)
	{
		const double D = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(A, B)));
		return FVector::DotProduct(FVector::CrossProduct(A, B), UpVector) < 0 ? 360 - D : D;
	}
}
