// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Lattice/PCGExLatticePaint.h"

#include "Paths/PCGExPolyPath.h"

namespace PCGExLattice
{
	void StairFill(const FPCGExLatticeBasis& Basis, const FIntVector& A, const FIntVector& B, TSet<FIntVector>& OutCoords, TArray<FIntVector>& OutSpine)
	{
		// Every accepted step sheds at least one unit of coord distance, so this bounds the walk -- an upper
		// bound, not the step count, since a diagonal step sheds more than one.
		const int32 MaxSteps = CoordL1(B - A);
		if (MaxSteps <= 0)
		{
			return;
		}

		const FVector WorldA = Basis.CoordToWorld(A);
		const FVector LineDir = (Basis.CoordToWorld(B) - WorldA).GetSafeNormal();
		// Deviations are squared world distances, so the tie test is squared too: far below any real
		// difference, but above the bit noise that would otherwise decide the diagonal-vs-zigzag tie.
		const double TieToleranceSq = FMath::Square(Basis.CellSize * 1.e-6);

		FIntVector Cur = A;
		for (int32 Guard = 0; Cur != B && Guard < MaxSteps; ++Guard)
		{
			if (OutCoords.Num() > MaxLatticeNodes)
			{
				return;
			} // runaway guard (tiny CellSize vs a long segment)

			const FIntVector Delta = B - Cur;
			const int32 Remaining = CoordL1(Delta);

			int32 BestStep = INDEX_NONE;
			int32 BestProgress = 0;
			double BestDeviationSq = 0.0;

			for (int32 s = 0; s < Basis.WalkOffsets.Num(); ++s)
			{
				const int32 Progress = Remaining - CoordL1(Delta - Basis.WalkOffsets[s]);
				if (Progress <= 0)
				{
					continue;
				} // never sideways or backwards -- forward-only progress is what bounds the walk

				// Perpendicular distance from the node this step lands on to the A->B line.
				const FVector Rel = Basis.CoordToWorld(Cur + Basis.WalkOffsets[s]) - WorldA;
				const double DeviationSq = (Rel - LineDir * FVector::DotProduct(Rel, LineDir)).SizeSquared();

				bool bBetter = BestStep == INDEX_NONE;
				if (!bBetter)
				{
					bBetter = DeviationSq < BestDeviationSq - TieToleranceSq
						? true
						: (DeviationSq > BestDeviationSq + TieToleranceSq ? false : Progress > BestProgress);
				}

				if (bBetter)
				{
					BestStep = s;
					BestProgress = Progress;
					BestDeviationSq = DeviationSq;
				}
			}

			if (BestStep == INDEX_NONE)
			{
				break;
			} // B not reachable within the basis subspace

			Cur += Basis.WalkOffsets[BestStep];
			OutCoords.Add(Cur);
			OutSpine.Add(Cur);
		}
	}

	void PaintLinear(const PCGExPaths::FPolyPath& Path, const FPCGExLatticeBasis& Basis, const double CaptureRadius, bool bForceClosed, TSet<FIntVector>& OutCoords)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::PaintLinear);

		const int32 NumPts = Path.NumPoints;
		if (NumPts == 0)
		{
			return;
		}

		// --- Spine ---
		TArray<FIntVector> Spine;
		FIntVector PrevCoord = Basis.SnapWorldToCoord(Path.GetPos(0));
		OutCoords.Add(PrevCoord);
		Spine.Add(PrevCoord);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::PaintLinear::Spine);

			const int32 NumEdges = (bForceClosed || Path.IsClosedLoop()) ? NumPts : NumPts - 1;
			for (int32 e = 0; e < NumEdges; ++e)
			{
				const int32 NextIdx = (e + 1) % NumPts; // wraps only when closed (open stops at NumPts-1)
				const FIntVector NextCoord = Basis.SnapWorldToCoord(Path.GetPos(NextIdx));
				StairFill(Basis, PrevCoord, NextCoord, OutCoords, Spine);
				PrevCoord = NextCoord;
			}
		}

		// --- Thickness (brush) ---
		if (CaptureRadius <= 0.0)
		{
			return;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::PaintLinear::Brush);

		const double RadiusSq = CaptureRadius * CaptureRadius;

		// Per-axis coord half-extent that provably covers a world sphere of radius CaptureRadius,
		// scaled by the basis anisotropy so skew (hex/triangular) lattices aren't clipped at large
		// radii. Callers should refuse a radius that needs more than MaxBrushHalfExtent, so the clamp
		// here only backstops a config that already errored out.
		const FIntVector Extent = Basis.CoordExtentForWorldRadius(CaptureRadius);
		const int32 HalfExtent[3] = {
			FMath::Clamp(Extent.X, 0, MaxBrushHalfExtent),
			FMath::Clamp(Extent.Y, 0, MaxBrushHalfExtent),
			FMath::Clamp(Extent.Z, 0, MaxBrushHalfExtent),
		};

		// Dedup spine coords so overlapping balls aren't re-enumerated from identical centres.
		TSet<FIntVector> UniqueSpine(Spine);
		for (const FIntVector& S : UniqueSpine)
		{
			if (OutCoords.Num() > MaxLatticeNodes)
			{
				return;
			} // runaway guard
			const FVector SWorld = Basis.CoordToWorld(S);
			for (int32 dz = -HalfExtent[2]; dz <= HalfExtent[2]; ++dz)
			{
				for (int32 dy = -HalfExtent[1]; dy <= HalfExtent[1]; ++dy)
				{
					for (int32 dx = -HalfExtent[0]; dx <= HalfExtent[0]; ++dx)
					{
						const FIntVector Cand(S.X + dx, S.Y + dy, S.Z + dz);
						if (FVector::DistSquared(Basis.CoordToWorld(Cand), SWorld) <= RadiusSq)
						{
							OutCoords.Add(Cand);
						}
					}
				}
			}
		}
	}

	double HalfCellEpsilon(const FPCGExLatticeBasis& Basis)
	{
		if (Basis.NumAxes <= 0)
		{
			return 0.0;
		}
		double Smallest = TNumericLimits<double>::Max();
		for (int32 k = 0; k < Basis.NumAxes; ++k)
		{
			Smallest = FMath::Min(Smallest, Basis.AxisVecs[k].Size());
		}
		return Smallest * 0.5;
	}

	double ProjectedSignedArea(const PCGExPaths::FPolyPath& Path)
	{
		const TArray<FVector2D>& P = Path.GetProjectedPoints();
		const int32 N = P.Num();
		if (N < 3)
		{
			return 0.0;
		}

		double Twice = 0.0;
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			Twice += (P[j].X * P[i].Y) - (P[i].X * P[j].Y);
		}
		return Twice * 0.5;
	}

	bool WorldBoundsToCoordBox(const FBox& WorldBounds, const FPCGExLatticeBasis& Basis, const double PadWorld, FIntVector& OutMin, FIntVector& OutMax,
	                           const FVector* ProjectionNormal)
	{
		if (!WorldBounds.IsValid)
		{
			return false;
		}

		FIntVector MinC(MAX_int32, MAX_int32, MAX_int32);
		FIntVector MaxC(MIN_int32, MIN_int32, MIN_int32);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector P(
				(Corner & 1) ? WorldBounds.Max.X : WorldBounds.Min.X,
				(Corner & 2) ? WorldBounds.Max.Y : WorldBounds.Min.Y,
				(Corner & 4) ? WorldBounds.Max.Z : WorldBounds.Min.Z);
			const FIntVector C = Basis.SnapWorldToCoord(P);
			MinC.X = FMath::Min(MinC.X, C.X);
			MinC.Y = FMath::Min(MinC.Y, C.Y);
			MinC.Z = FMath::Min(MinC.Z, C.Z);
			MaxC.X = FMath::Max(MaxC.X, C.X);
			MaxC.Y = FMath::Max(MaxC.Y, C.Y);
			MaxC.Z = FMath::Max(MaxC.Z, C.Z);
		}

		// A cell of slack absorbs the corner-snap rounding. Never on a non-axis: coords beyond NumAxes
		// don't move the world position, so padding one revisits the same spot.
		FIntVector Pad = Basis.CoordExtentForWorldRadius(FMath::Max(0.0, PadWorld));
		if (Basis.NumAxes > 0)
		{
			Pad.X = FMath::Max(Pad.X, 1);
		}
		if (Basis.NumAxes > 1)
		{
			Pad.Y = FMath::Max(Pad.Y, 1);
		}
		if (Basis.NumAxes > 2)
		{
			Pad.Z = FMath::Max(Pad.Z, 1);
		}

		if (ProjectionNormal)
		{
			int32* const PadComp[3] = {&Pad.X, &Pad.Y, &Pad.Z};
			for (int32 k = 0; k < Basis.NumAxes; ++k)
			{
				if (FMath::Abs(FVector::DotProduct(Basis.AxisVecs[k].GetSafeNormal(), *ProjectionNormal)) > 0.5)
				{
					*PadComp[k] = 0;
				}
			}
		}

		// Pad reaches ~1e9 cells: int32 would wrap the box negative and silently sweep nothing.
		auto Offset = [](const int32 Base, const int64 Delta) -> int32
		{
			return static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(Base) + Delta, MIN_int32 / 2, MAX_int32 / 2));
		};
		OutMin = FIntVector(Offset(MinC.X, -Pad.X), Offset(MinC.Y, -Pad.Y), Offset(MinC.Z, -Pad.Z));
		OutMax = FIntVector(Offset(MaxC.X, Pad.X), Offset(MaxC.Y, Pad.Y), Offset(MaxC.Z, Pad.Z));
		return true;
	}

	bool PaintSurface(const PCGExPaths::FPolyPath& Path, const FPCGExLatticeBasis& Basis, const double CaptureRadius, bool bForceClosed, TSet<FIntVector>& OutCoords)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::PaintSurface);

		if (!bForceClosed && !Path.IsClosedLoop())
		{
			PaintLinear(Path, Basis, CaptureRadius, bForceClosed, OutCoords);
			return true;
		}

		int32 InsideCount = 0;
		FIntVector MinC, MaxC;
		const FVector ProjNormal = Path.GetProjection().ProjectionQuat.RotateVector(FVector::UpVector);
		if (WorldBoundsToCoordBox(Path.Bounds, Basis, HalfCellEpsilon(Basis) + CaptureRadius, MinC, MaxC, &ProjNormal))
		{
			// An eroded-away outline or a thin sliver in a huge AABB accepts nothing, so the node ceiling
			// never moves and only a visit budget bounds the sweep.
			if (CoordBoxCellCount(MinC, MaxC) > MaxLatticeCellVisits)
			{
				return false;
			}

			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::PaintSurface::Fill);

			for (int32 z = MinC.Z; z <= MaxC.Z; ++z)
			{
				for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
				{
					if (OutCoords.Num() > MaxLatticeNodes)
					{
						return true;
					} // runaway guard, per row
					for (int32 x = MinC.X; x <= MaxC.X; ++x)
					{
						const FIntVector Cand(x, y, z);
						if (Path.IsInsideProjection(Basis.CoordToWorld(Cand)))
						{
							++InsideCount;
							OutCoords.Add(Cand);
						}
					}
				}
			}
		}

		if (InsideCount == 0 && CaptureRadius >= 0.0)
		{
			PaintLinear(Path, Basis, CaptureRadius, bForceClosed, OutCoords);
		}
		return true;
	}

	bool SubtractHole(const PCGExPaths::FPolyPath& Path, const FPCGExLatticeBasis& Basis, const double CaptureRadius, TSet<FIntVector>& OutCoords)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::SubtractHole);

		if (OutCoords.IsEmpty())
		{
			return true;
		}

		FIntVector MinC, MaxC;
		const FVector ProjNormal = Path.GetProjection().ProjectionQuat.RotateVector(FVector::UpVector);
		if (!WorldBoundsToCoordBox(Path.Bounds, Basis, CaptureRadius - HalfCellEpsilon(Basis), MinC, MaxC, &ProjNormal))
		{
			return true;
		}

		// Removal-only, so no output-based ceiling can ever bound this.
		if (CoordBoxCellCount(MinC, MaxC) > MaxLatticeCellVisits)
		{
			return false;
		}

		for (int32 z = MinC.Z; z <= MaxC.Z; ++z)
		{
			for (int32 y = MinC.Y; y <= MaxC.Y; ++y)
			{
				for (int32 x = MinC.X; x <= MaxC.X; ++x)
				{
					const FIntVector Cand(x, y, z);
					if (Path.IsInsideProjection(Basis.CoordToWorld(Cand)))
					{
						OutCoords.Remove(Cand);
					}
				}
			}
		}
		return true;
	}

	FVector ComputeDataBaseOffset(const TArray<TSharedPtr<PCGExPaths::FPolyPath>>& Shapes, const TArray<FBox>& VolumeEntryBounds, const FPCGExLatticeBasis& Basis, EPCGExLatticeBaseMode Mode)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExLattice::ComputeDataBaseOffset);

		if (Mode == EPCGExLatticeBaseMode::Flat)
		{
			return FVector::ZeroVector;
		}

		FVector Complement[3];
		const int32 NumComplement = Basis.GetComplementBasis(Complement);
		if (NumComplement <= 0)
		{
			return FVector::ZeroVector;
		}

		double MinD[3] = {TNumericLimits<double>::Max(), TNumericLimits<double>::Max(), TNumericLimits<double>::Max()};
		double MaxD[3] = {-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max(), -TNumericLimits<double>::Max()};
		double SumD[3] = {0.0, 0.0, 0.0};
		int64 Count = 0;

		auto Accumulate = [&](const FVector& WorldPos)
		{
			const FVector D = WorldPos - Basis.Origin;
			for (int32 j = 0; j < NumComplement; ++j)
			{
				const double Dist = FVector::DotProduct(D, Complement[j]);
				MinD[j] = FMath::Min(MinD[j], Dist);
				MaxD[j] = FMath::Max(MaxD[j], Dist);
				SumD[j] += Dist;
			}
			++Count;
		};

		for (const TSharedPtr<PCGExPaths::FPolyPath>& Path : Shapes)
		{
			if (!Path.IsValid())
			{
				continue;
			}
			const int32 NumPts = Path->NumPoints;
			for (int32 i = 0; i < NumPts; ++i)
			{
				Accumulate(Path->GetPos(i));
			}
		}

		// Occupancy volumes are data too -- sample each entry's bounds corners so a volumes-only planar
		// lattice shifts its plane onto the volume (shapes-only inference would leave it empty at
		// Origin's plane).
		for (const FBox& Bounds : VolumeEntryBounds)
		{
			if (!Bounds.IsValid)
			{
				continue;
			}
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				Accumulate(FVector(
					(Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
					(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
					(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z));
			}
		}

		if (Count == 0)
		{
			return FVector::ZeroVector;
		}

		FVector Base = FVector::ZeroVector;
		for (int32 j = 0; j < NumComplement; ++j)
		{
			double B = 0.0;
			switch (Mode)
			{
			case EPCGExLatticeBaseMode::Min:
				B = MinD[j];
				break;
			case EPCGExLatticeBaseMode::Max:
				B = MaxD[j];
				break;
			case EPCGExLatticeBaseMode::Average:
				B = SumD[j] / static_cast<double>(Count);
				break;
			default:
				break;
			}
			Base += Complement[j] * B;
		}
		return Base;
	}
}
