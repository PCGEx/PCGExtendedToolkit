// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Lattice/PCGExLatticeBasis.h"

bool FPCGExLatticeBasis::BuildFromSteps(TConstArrayView<FVector> InDirections, TConstArrayView<double> InLengthMultipliers, double InCellSize, const FVector& InOrigin, const FQuat& InFrameRot)
{
	bValid = false;
	NumAxes = 0;
	NumSteps = 0;
	UnresolvedSteps.Reset();
	WalkOffsets.Reset();
	StepDirs.Reset();
	StepVectors.Reset();
	StepOffsets.Reset();
	FrameRotation = InFrameRot;
	for (int32 k = 0; k < 3; ++k)
	{
		AxisVecs[k] = FVector::ZeroVector;
		UnitAxis[k] = FVector::ZeroVector;
		AxisLen[k] = 0.0;
		AxisStep[k] = INDEX_NONE;
	}
	Origin = InOrigin;
	CellSize = InCellSize;

	if (InDirections.Num() == 0 || InDirections.Num() != InLengthMultipliers.Num() || !(InCellSize > 0.0))
	{
		return false;
	}

	NumSteps = InDirections.Num();
	StepDirs.SetNumUninitialized(NumSteps);
	StepVectors.SetNumUninitialized(NumSteps);
	StepOffsets.SetNumZeroed(NumSteps);

	// Per-step normalized dirs + world vectors (rigid frame rotation applied to both).
	for (int32 i = 0; i < NumSteps; ++i)
	{
		FVector Dir = InDirections[i].GetSafeNormal();
		if (!Dir.IsNearlyZero())
		{
			Dir = InFrameRot.RotateVector(Dir);
		}
		StepDirs[i] = Dir;
		StepVectors[i] = Dir * (InLengthMultipliers[i] * InCellSize);
	}

	// No opposition map here on purpose: edge tagging resolves BOTH endpoints through StepOffsets,
	// the only answer consistent with per-node masks. A consumer that needs opposites should derive
	// them from whatever authored the step set rather than re-derive the rule here.

	// Axis selection: greedy SHORTEST-STEP-FIRST over the step directions, with a Gram-Schmidt
	// residual on UNIT directions deciding independence so opposites and diagonals fall out on their own.
	//
	// Length is the primary key because a basis has to GENERATE the lattice, and it is the shortest
	// independent steps that do -- for rank <= 3 (our cap) steps realizing the successive minima always
	// form a basis. Selecting for orthogonality instead picks a coarse SUBLATTICE: on a compass set the
	// two perpendicular diagonals win, every cardinal then sits on a half-integer coordinate and resolves
	// to nothing, and no tolerance can rescue it because the cardinals genuinely are not nodes of the
	// lattice the diagonals generate. It would also let floating point decide, since a pre-normalized
	// diagonal (0.707107, ...) squares two ULP ABOVE an axis-aligned cardinal's exact 1.0 -- which family
	// became the axes would depend on the digits typed into the asset.
	//
	// Residual breaks length ties, which is the common case: a uniform-length cardinal or hex set ties
	// everywhere here, so the most-perpendicular step wins. The length comparison carries a relative
	// epsilon of its own, since authored lengths are decimals and two steps meant to be equal need not
	// compare equal. Ties break to the lowest index, keeping this deterministic.
	constexpr double IndependenceEpsSq = 1.e-6;
	constexpr double LengthTieTolerance = 1.e-6;
	FVector Span[3] = {};
	while (NumAxes < 3)
	{
		int32 BestIdx = INDEX_NONE;
		double BestLen = 0.0;
		double BestResidualSq = 0.0;
		FVector BestResidual = FVector::ZeroVector;

		for (int32 i = 0; i < NumSteps; ++i)
		{
			if (StepDirs[i].IsNearlyZero() || StepVectors[i].IsNearlyZero())
			{
				continue;
			}

			FVector Residual = StepDirs[i];
			for (int32 s = 0; s < NumAxes; ++s)
			{
				Residual -= Span[s] * FVector::DotProduct(Residual, Span[s]);
			}

			const double ResidualSq = Residual.SizeSquared();
			if (ResidualSq <= IndependenceEpsSq)
			{
				continue;
			} // dependent on the axes already taken

			const double Len = StepVectors[i].Size();
			bool bBetter = BestIdx == INDEX_NONE;
			if (!bBetter)
			{
				const double LenEps = FMath::Max(Len, BestLen) * LengthTieTolerance;
				bBetter = Len < BestLen - LenEps ? true : (Len > BestLen + LenEps ? false : ResidualSq > BestResidualSq);
			}

			if (bBetter)
			{
				BestIdx = i;
				BestLen = Len;
				BestResidualSq = ResidualSq;
				BestResidual = Residual;
			}
		}

		if (BestIdx == INDEX_NONE)
		{
			break;
		}

		UnitAxis[NumAxes] = StepDirs[BestIdx];
		AxisLen[NumAxes] = BestLen;
		AxisVecs[NumAxes] = StepVectors[BestIdx];
		AxisStep[NumAxes] = BestIdx;
		Span[NumAxes] = BestResidual.GetSafeNormal();
		++NumAxes;
	}

	if (NumAxes == 0 || !ComputeUnitGramInverse())
	{
		// All steps collinear / zero, or a degenerate basis. Consumer warns + skips.
		NumAxes = 0;
		return false;
	}

	// A step only earns an offset when the node it names is unambiguous. Its DIRECTION is what
	// identifies that neighbour; the authored length merely says how far, and authors write it as a
	// decimal -- a square diagonal is 1.41, not 1.4142135624. So round to the nearest coordinate, then
	// accept it only if the offset's own lattice vector really is the direction that was drawn (ANGLE,
	// loose enough to absorb a truncated decimal) and the authored step really is that far (MAGNITUDE,
	// relative). Testing the rounding residual in coordinate space instead spends one absolute tolerance
	// on both questions and answers neither well: at 1e-3 of a cell it rejects 1.41, which misses sqrt(2)
	// by 0.3%, and so drops every diagonal in the set.
	//
	// The magnitude gate is what keeps a step pointing genuinely BETWEEN nodes out: a 45-degree
	// diagonal authored at length 1.0 is 29% short of the lattice diagonal and stays unresolved, where
	// 1.41 is 0.3% short and resolves. Resolving one regardless would hand a step that never reaches its
	// neighbour that neighbour's coordinates, mis-describing every edge and mask bit it owns and letting
	// two distinct steps collapse onto one offset. Unresolved steps get the zero offset consumers
	// already skip, plus their index so the caller can name them.
	constexpr double AngularToleranceCos = 0.9998476951563913; // cos(1 degree)
	constexpr double MagnitudeTolerance = 0.1;                 // fraction of the true lattice distance
	for (int32 i = 0; i < NumSteps; ++i)
	{
		double X[3];
		ComputeContinuousLocal(StepVectors[i], X);

		const FIntVector Offset(FMath::RoundToInt32(X[0]), FMath::RoundToInt32(X[1]), FMath::RoundToInt32(X[2]));

		// Exact world vector of that offset -- the step taken if it resolves. Summed from AxisVecs
		// rather than through CoordToWorld so a far-from-zero Origin can't cancel precision.
		FVector LatticeVec = FVector::ZeroVector;
		if (NumAxes > 0)
		{
			LatticeVec += AxisVecs[0] * static_cast<double>(Offset.X);
		}
		if (NumAxes > 1)
		{
			LatticeVec += AxisVecs[1] * static_cast<double>(Offset.Y);
		}
		if (NumAxes > 2)
		{
			LatticeVec += AxisVecs[2] * static_cast<double>(Offset.Z);
		}

		const double LatticeLen = LatticeVec.Size();
		bool bResolved = Offset != FIntVector::ZeroValue && LatticeLen > UE_DOUBLE_SMALL_NUMBER;
		if (bResolved)
		{
			bResolved = FVector::DotProduct(LatticeVec / LatticeLen, StepDirs[i]) >= AngularToleranceCos &&
				FMath::Abs(LatticeLen - StepVectors[i].Size()) <= MagnitudeTolerance * LatticeLen;
		}

		if (bResolved)
		{
			StepOffsets[i] = Offset;
			// Snap the vector onto the node it names, so a decimal-authored length leaves no consumer
			// short of the neighbour its offset points at.
			StepVectors[i] = LatticeVec;
		}
		else
		{
			StepOffsets[i] = FIntVector::ZeroValue;
			if (!StepVectors[i].IsNearlyZero())
			{
				UnresolvedSteps.Add(i);
			}
		}
	}

	// Legal single-step moves for anything that walks the lattice. Axes first, because they are generators:
	// seeding them lets a walk close the gap to any coordinate even if every non-axis step failed to
	// resolve. Then each resolved offset and its negation, in step index order.
	WalkOffsets.Reset(2 * (NumAxes + NumSteps));
	for (int32 k = 0; k < NumAxes; ++k)
	{
		const FIntVector Unit = AxisUnitOffset(k);
		WalkOffsets.AddUnique(Unit);
		WalkOffsets.AddUnique(FIntVector(-Unit.X, -Unit.Y, -Unit.Z));
	}
	for (int32 i = 0; i < NumSteps; ++i)
	{
		const FIntVector& Offset = StepOffsets[i];
		if (Offset == FIntVector::ZeroValue)
		{
			continue;
		}
		WalkOffsets.AddUnique(Offset);
		WalkOffsets.AddUnique(FIntVector(-Offset.X, -Offset.Y, -Offset.Z));
	}

	bValid = true;
	return true;
}

bool FPCGExLatticeBasis::ComputeUnitGramInverse()
{
	for (int32 i = 0; i < 3; ++i)
	{
		for (int32 j = 0; j < 3; ++j)
		{
			UnitGramInv[i][j] = 0.0;
		}
	}

	// Unit Gram G = UnitAxis^T UnitAxis (symmetric, dimensionless). Diagonal entries are 1.
	double G[3][3] = {};
	for (int32 i = 0; i < NumAxes; ++i)
	{
		for (int32 j = 0; j < NumAxes; ++j)
		{
			G[i][j] = FVector::DotProduct(UnitAxis[i], UnitAxis[j]);
		}
	}

	if (NumAxes == 1)
	{
		if (FMath::Abs(G[0][0]) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		UnitGramInv[0][0] = 1.0 / G[0][0];
		return true;
	}

	if (NumAxes == 2)
	{
		const double Det = G[0][0] * G[1][1] - G[0][1] * G[1][0];
		if (FMath::Abs(Det) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		const double Inv = 1.0 / Det;
		UnitGramInv[0][0] = G[1][1] * Inv;
		UnitGramInv[0][1] = -G[0][1] * Inv;
		UnitGramInv[1][0] = -G[1][0] * Inv;
		UnitGramInv[1][1] = G[0][0] * Inv;
		return true;
	}

	// NumAxes == 3 -- adjugate / determinant (G symmetric).
	const double C00 = (G[1][1] * G[2][2] - G[1][2] * G[2][1]);
	const double C01 = -(G[1][0] * G[2][2] - G[1][2] * G[2][0]);
	const double C02 = (G[1][0] * G[2][1] - G[1][1] * G[2][0]);
	const double Det = G[0][0] * C00 + G[0][1] * C01 + G[0][2] * C02;
	if (FMath::Abs(Det) <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const double Inv = 1.0 / Det;

	UnitGramInv[0][0] = (G[1][1] * G[2][2] - G[1][2] * G[2][1]) * Inv;
	UnitGramInv[0][1] = -(G[0][1] * G[2][2] - G[0][2] * G[2][1]) * Inv;
	UnitGramInv[0][2] = (G[0][1] * G[1][2] - G[0][2] * G[1][1]) * Inv;
	UnitGramInv[1][0] = -(G[1][0] * G[2][2] - G[1][2] * G[2][0]) * Inv;
	UnitGramInv[1][1] = (G[0][0] * G[2][2] - G[0][2] * G[2][0]) * Inv;
	UnitGramInv[1][2] = -(G[0][0] * G[1][2] - G[0][2] * G[1][0]) * Inv;
	UnitGramInv[2][0] = (G[1][0] * G[2][1] - G[1][1] * G[2][0]) * Inv;
	UnitGramInv[2][1] = -(G[0][0] * G[2][1] - G[0][1] * G[2][0]) * Inv;
	UnitGramInv[2][2] = (G[0][0] * G[1][1] - G[0][1] * G[1][0]) * Inv;
	return true;
}

void FPCGExLatticeBasis::ComputeContinuousLocal(const FVector& Displacement, double OutX[3]) const
{
	// Solve in the unit basis: y = UnitGramInv * (UnitAxis^T Displacement), then divide out the
	// per-axis length to recover integer-scaled coordinates.
	double B[3] = {0.0, 0.0, 0.0};
	for (int32 i = 0; i < NumAxes; ++i)
	{
		B[i] = FVector::DotProduct(UnitAxis[i], Displacement);
	}

	OutX[0] = OutX[1] = OutX[2] = 0.0;
	for (int32 i = 0; i < NumAxes; ++i)
	{
		double Acc = 0.0;
		for (int32 j = 0; j < NumAxes; ++j)
		{
			Acc += UnitGramInv[i][j] * B[j];
		}
		OutX[i] = Acc / AxisLen[i]; // AxisLen[i] > 0 guaranteed by axis selection
	}
}

FIntVector FPCGExLatticeBasis::SnapWorldToCoord(const FVector& World) const
{
	double X[3];
	ComputeContinuousLocal(World - Origin, X);
	const FIntVector Rounded(FMath::RoundToInt32(X[0]), FMath::RoundToInt32(X[1]), FMath::RoundToInt32(X[2]));

	// Rounding each coordinate independently (Babai) picks the node whose PARALLELEPIPED the point sits
	// in, which is the nearest node only when the basis is orthogonal. On a 60-degree hex basis the
	// parallelogram and the hexagonal Voronoi cell disagree over 1/6 of every cell, and the point can land
	// 0.36 cells further away than the true nearest -- quantizing hex inputs onto a rhombic pattern.
	//
	// Rounding is still the right starting point: it is off by at most one coordinate step, so a fixed
	// +-1 search around it recovers the true nearest. Verified exact on square, hex, anisotropic hex,
	// 20-degree skew, cubic, hex-prism and fully sheared 3D bases. On an orthogonal basis the rounded
	// coordinate always wins, so the search costs a few distance tests and changes nothing.
	FIntVector Best = Rounded;
	double BestDistSq = FVector::DistSquared(CoordToWorld(Rounded), World);

	const int32 Range[3] = {NumAxes > 0 ? 1 : 0, NumAxes > 1 ? 1 : 0, NumAxes > 2 ? 1 : 0};
	for (int32 dz = -Range[2]; dz <= Range[2]; ++dz)
	{
		for (int32 dy = -Range[1]; dy <= Range[1]; ++dy)
		{
			for (int32 dx = -Range[0]; dx <= Range[0]; ++dx)
			{
				if (dx == 0 && dy == 0 && dz == 0)
				{
					continue;
				}

				const FIntVector Candidate(Rounded.X + dx, Rounded.Y + dy, Rounded.Z + dz);
				const double DistSq = FVector::DistSquared(CoordToWorld(Candidate), World);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Best = Candidate;
				}
			}
		}
	}

	return Best;
}

FIntVector FPCGExLatticeBasis::SnapWorldToCoordPreserving(const FVector& World, const FIntVector& Previous) const
{
	FIntVector Coord = SnapWorldToCoord(World);
	if (NumAxes <= 2)
	{
		Coord.Z = Previous.Z;
	}
	if (NumAxes <= 1)
	{
		Coord.Y = Previous.Y;
	}
	if (NumAxes <= 0)
	{
		Coord.X = Previous.X;
	}
	return Coord;
}

FVector FPCGExLatticeBasis::ContinuousCoord(const FVector& World) const
{
	double X[3];
	ComputeContinuousLocal(World - Origin, X);
	return FVector(X[0], X[1], X[2]);
}

FVector FPCGExLatticeBasis::CoordToWorld(const FIntVector& Coord) const
{
	FVector P = Origin;
	if (NumAxes > 0)
	{
		P += AxisVecs[0] * static_cast<double>(Coord.X);
	}
	if (NumAxes > 1)
	{
		P += AxisVecs[1] * static_cast<double>(Coord.Y);
	}
	if (NumAxes > 2)
	{
		P += AxisVecs[2] * static_cast<double>(Coord.Z);
	}
	return P;
}

FIntVector FPCGExLatticeBasis::AxisUnitOffset(int32 AxisIndex) const
{
	// Only [0..NumAxes) are real derived axes; anything else is out of range -> zero (contract).
	if (AxisIndex < 0 || AxisIndex >= NumAxes)
	{
		return FIntVector(0, 0, 0);
	}
	FIntVector Offset(0, 0, 0);
	if (AxisIndex == 0)
	{
		Offset.X = 1;
	}
	else if (AxisIndex == 1)
	{
		Offset.Y = 1;
	}
	else if (AxisIndex == 2)
	{
		Offset.Z = 1;
	}
	return Offset;
}

int32 FPCGExLatticeBasis::GetComplementBasis(FVector OutDirs[3]) const
{
	OutDirs[0] = OutDirs[1] = OutDirs[2] = FVector::ZeroVector;
	if (NumAxes >= 3)
	{
		return 0;
	} // the lattice already spans every direction

	constexpr double EpsSq = 1.e-6;

	// Orthonormalize the lattice axes (they may be skew) to get the spanned subspace.
	FVector Span[3];
	int32 SpanCount = 0;
	for (int32 k = 0; k < NumAxes; ++k)
	{
		FVector R = UnitAxis[k];
		for (int32 s = 0; s < SpanCount; ++s)
		{
			R -= Span[s] * FVector::DotProduct(R, Span[s]);
		}
		if (R.SizeSquared() > EpsSq)
		{
			Span[SpanCount++] = R.GetSafeNormal();
		}
	}

	// Gram-Schmidt the world axes against the span (and each accepted complement dir) -- whatever
	// survives is orthogonal to the lattice, i.e. the directions snapping throws away.
	const FVector WorldAxes[3] = {FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1)};
	int32 Count = 0;
	for (int32 w = 0; w < 3 && Count < (3 - SpanCount); ++w)
	{
		FVector R = WorldAxes[w];
		for (int32 s = 0; s < SpanCount; ++s)
		{
			R -= Span[s] * FVector::DotProduct(R, Span[s]);
		}
		for (int32 c = 0; c < Count; ++c)
		{
			R -= OutDirs[c] * FVector::DotProduct(R, OutDirs[c]);
		}
		if (R.SizeSquared() > EpsSq)
		{
			OutDirs[Count++] = R.GetSafeNormal();
		}
	}
	return Count;
}

FIntVector FPCGExLatticeBasis::CoordExtentForWorldRadius(double WorldRadius) const
{
	FIntVector Out(0, 0, 0);
	if (!(WorldRadius > 0.0))
	{
		return Out;
	}

	int32* const Comp[3] = {&Out.X, &Out.Y, &Out.Z};
	for (int32 k = 0; k < NumAxes; ++k)
	{
		// max |coord_k| over the world ball of radius R = R * |dual basis vector k|
		//   = R * sqrt(UnitGramInv[k][k]) / AxisLen[k]  (UnitGramInv diagonal >= 1; AxisLen[k] > 0).
		// The scaled radius is clamped before rounding so a tiny AxisLen can't overflow CeilToInt32.
		const double DualNorm = FMath::Sqrt(FMath::Max(UnitGramInv[k][k], 0.0)) / AxisLen[k];
		const double Cells = FMath::Min(WorldRadius * DualNorm, 1.0e9);
		*Comp[k] = FMath::CeilToInt32(Cells) + 1;
	}
	return Out;
}
