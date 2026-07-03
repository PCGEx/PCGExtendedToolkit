// Copyright 2026 Timothe Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClipper2Decomposition.h"

#include "Algo/Reverse.h"
#include "Clipper2Lib/clipper.triangulation.h"
#include "Internationalization/Text.h" // FText / LOCTEXT used by DescribeDecomposeFailure

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Math/Geo/PCGExGeo.h"

#define LOCTEXT_NAMESPACE "PCGExClipper2Decomposition"

// File-local helpers (named namespace -- Unity-build safe).
namespace PCGExClipper2Decomposition
{
	// 2D cross of (A-O),(B-O); >0 when O->A->B turns left (CCW).
	FORCEINLINE double Cross2D(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return PCGExMath::Geo::Det(A - O, B - O);
	}

	double SignedArea(const TArray<int32>& Loop, const TArray<FFootprintVertex>& Pool)
	{
		double Area = 0;
		const int32 N = Loop.Num();
		for (int32 i = 0; i < N; i++)
		{
			const FVector2D& A = Pool[Loop[i]].Pos;
			const FVector2D& B = Pool[Loop[(i + 1) % N]].Pos;
			Area += PCGExMath::Geo::Det(A, B); // shoelace term
		}
		return 0.5 * Area;
	}

	// Reorder loop to CCW winding (positive signed area).
	void EnsureCCW(TArray<int32>& Loop, const TArray<FFootprintVertex>& Pool)
	{
		if (SignedArea(Loop, Pool) < 0)
		{
			Algo::Reverse(Loop);
		}
	}

	// Convex test assuming CCW winding; near-collinear allowed (reflex -> false).
	bool IsConvexCCW(const TArray<int32>& Loop, const TArray<FFootprintVertex>& Pool)
	{
		const int32 N = Loop.Num();
		if (N < 3)
		{
			return false;
		}
		for (int32 i = 0; i < N; i++)
		{
			const FVector2D& O = Pool[Loop[i]].Pos;
			const FVector2D& A = Pool[Loop[(i + 1) % N]].Pos;
			const FVector2D& B = Pool[Loop[(i + 2) % N]].Pos;
			if (Cross2D(O, A, B) < -UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
		}
		return true;
	}

	// Merge A and B (both CCW) along their shared edge iff the union stays convex. Two disjoint convex
	// polygons share at most one edge, so the first matching half-edge is the only candidate.
	bool TryMergeConvex(const TArray<int32>& A, const TArray<int32>& B, const TArray<FFootprintVertex>& Pool, TArray<int32>& OutMerged)
	{
		const int32 NA = A.Num();
		const int32 NB = B.Num();

		for (int32 ia = 0; ia < NA; ia++)
		{
			const int32 U = A[ia];
			const int32 V = A[(ia + 1) % NA];

			for (int32 ib = 0; ib < NB; ib++)
			{
				if (B[ib] != V || B[(ib + 1) % NB] != U)
				{
					continue;
				}

				// Merged loop: A from V around to U, then B's interior.
				TArray<int32> Merged;
				Merged.Reserve(NA + NB - 2);
				for (int32 k = 0; k < NA; k++)
				{
					Merged.Add(A[(ia + 1 + k) % NA]);
				} // V ... U
				for (int32 k = 0; k < NB - 2; k++)
				{
					Merged.Add(B[(ib + 2 + k) % NB]);
				} // interior of B

				if (Merged.Num() >= 3 && IsConvexCCW(Merged, Pool))
				{
					OutMerged = MoveTemp(Merged);
					return true;
				}
				return false; // only candidate edge, union not convex
			}
		}
		return false;
	}

	// Greedy Hertel-Mehlhorn convex merge of a triangulation into fewer convex pieces.
	// Restarting the i/j scan after every merge is intentional: greedy merging is order-sensitive, and
	// re-pairing a freshly-grown piece yields fewer pieces than a forward sweep (measured). O(n^3) but worth it.
	// The Bounds pre-filter is exact -- pieces sharing an edge share 2 endpoints, so non-overlapping bounds
	// can only skip pairs TryMergeConvex would reject anyway.
	void MergeIntoConvexPieces(TArray<TArray<int32>>& Pieces, const TArray<FFootprintVertex>& Pool)
	{
		// Tight 2D bounds of a piece (always >= 3 vertices, so Loop[0] is valid).
		struct FBounds2D
		{
			double MinX, MinY, MaxX, MaxY;
		};
		auto ComputeBounds = [&Pool](const TArray<int32>& Loop) -> FBounds2D
		{
			const FVector2D& P0 = Pool[Loop[0]].Pos;
			FBounds2D B{P0.X, P0.Y, P0.X, P0.Y};
			for (int32 k = 1; k < Loop.Num(); k++)
			{
				const FVector2D& P = Pool[Loop[k]].Pos;
				B.MinX = FMath::Min(B.MinX, P.X);
				B.MinY = FMath::Min(B.MinY, P.Y);
				B.MaxX = FMath::Max(B.MaxX, P.X);
				B.MaxY = FMath::Max(B.MaxY, P.Y);
			}
			return B;
		};

		// Bounds[k] tracks Pieces[k] in lockstep.
		TArray<FBounds2D> Bounds;
		Bounds.Reserve(Pieces.Num());
		for (const TArray<int32>& Piece : Pieces)
		{
			Bounds.Add(ComputeBounds(Piece));
		}

		constexpr double Eps = UE_KINDA_SMALL_NUMBER;

		bool bMerged = true;
		while (bMerged && Pieces.Num() > 1)
		{
			bMerged = false;
			for (int32 i = 0; i < Pieces.Num() && !bMerged; i++)
			{
				for (int32 j = i + 1; j < Pieces.Num() && !bMerged; j++)
				{
					// Separated bounds -> no shared edge -> can't merge (exact pre-filter).
					const FBounds2D& Bi = Bounds[i];
					const FBounds2D& Bj = Bounds[j];
					if (Bi.MinX > Bj.MaxX + Eps || Bj.MinX > Bi.MaxX + Eps ||
						Bi.MinY > Bj.MaxY + Eps || Bj.MinY > Bi.MaxY + Eps)
					{
						continue;
					}

					TArray<int32> Result;
					if (TryMergeConvex(Pieces[i], Pieces[j], Pool, Result))
					{
						Pieces[i] = MoveTemp(Result);
						Bounds[i] = ComputeBounds(Pieces[i]); // grown piece -> refresh its bounds
						Pieces.RemoveAt(j);
						Bounds.RemoveAt(j); // keep Bounds aligned with Pieces
						bMerged = true;
					}
				}
			}
		}
	}

	FDecomposeResult Decompose(
		const PCGExClipper2Lib::Paths64& SubjectPaths,
		const TSharedPtr<PCGExClipper2::FOpData>& AllOpData,
		const PCGExClipper2Lib::ZCallback64& ZCallback,
		const FDecomposeParams& Params)
	{
		FDecomposeResult Out;

		const double InvScale = 1.0 / static_cast<double>(Params.Precision);

		// --- Boundary-respecting triangulation (holes honored via fill rule) ---
		PCGExClipper2Lib::Paths64 TrianglePaths;
		const PCGExClipper2Lib::TriangulateResult Result = PCGExClipper2Lib::TriangulateWithHoles(
			SubjectPaths, TrianglePaths, PCGExClipper2::ConvertFillRule(Params.FillRule), Params.bUseDelaunay, ZCallback);

		if (Result != PCGExClipper2Lib::TriangulateResult::success || TrianglePaths.empty())
		{
			Out.Status = EDecomposeResult::TriangulationFailed;
			return Out;
		}

		// --- Deduplicated 2D vertex pool, each vertex mapped back to its source point when possible ---
		const int32 EstimatedVerts = static_cast<int32>(TrianglePaths.size()) * 3;
		TMap<uint64, int32> VertexMap;
		Out.VertexPool.Reserve(EstimatedVerts);
		VertexMap.Reserve(EstimatedVerts);

		auto FindOrAddVertex = [&](const PCGExClipper2Lib::Point64& Pt) -> int32
		{
			// Dedup key packs the low 32 bits of each int64 Clipper coord. Exact while |x|,|y| < 2^31 scaled
			// units (~215km at Precision=100, ~2km at 10000); beyond that, dropped high bits can weld distinct
			// verts. If it bites, key on the full int64 pair instead.
			const uint64 Hash = PCGEx::H64(static_cast<uint32>(Pt.x & 0xFFFFFFFF), static_cast<uint32>(Pt.y & 0xFFFFFFFF));
			if (const int32* Found = VertexMap.Find(Hash))
			{
				return *Found;
			}

			FFootprintVertex V;
			V.Pos = FVector2D(static_cast<double>(Pt.x) * InvScale, static_cast<double>(Pt.y) * InvScale);

			uint32 RawPointIdx, RawSourceIdx;
			PCGEx::H64(static_cast<uint64>(Pt.z), RawPointIdx, RawSourceIdx);

			if (RawPointIdx != PCGExClipper2::INTERSECTION_MARKER)
			{
				const int32 SrcIdx = static_cast<int32>(RawSourceIdx);
				const int32 PtIdx = static_cast<int32>(RawPointIdx);

				if (AllOpData->Facades.IsValidIndex(SrcIdx))
				{
					const int32 SrcNum = AllOpData->Facades[SrcIdx]->Source->GetNum(PCGExData::EIOSide::In);
					if (PtIdx < SrcNum)
					{
						V.SourceIdx = SrcIdx;
						V.SourcePointIdx = PtIdx;
						V.bHasSource = true;

						if (AllOpData->ProjectedZValues.IsValidIndex(SrcIdx) && AllOpData->ProjectedZValues[SrcIdx].IsValidIndex(PtIdx))
						{
							V.ProjectedZ = AllOpData->ProjectedZValues[SrcIdx][PtIdx];
						}
					}
				}
			}

			const int32 Index = Out.VertexPool.Num();
			VertexMap.Add(Hash, Index);
			Out.VertexPool.Add(V);
			return Index;
		};

		Out.Pieces.Reserve(static_cast<int32>(TrianglePaths.size()));
		for (const auto& Tri : TrianglePaths)
		{
			if (Tri.size() != 3)
			{
				continue;
			}
			const int32 A = FindOrAddVertex(Tri[0]);
			const int32 B = FindOrAddVertex(Tri[1]);
			const int32 C = FindOrAddVertex(Tri[2]);
			if (A == B || B == C || C == A)
			{
				continue;
			}

			TArray<int32> Piece = {A, B, C};
			EnsureCCW(Piece, Out.VertexPool);
			Out.Pieces.Add(MoveTemp(Piece));
		}

		if (Out.Pieces.IsEmpty() || Out.VertexPool.IsEmpty())
		{
			Out.Status = EDecomposeResult::Empty;
			return Out;
		}

		if (Params.bMergeConvexPieces)
		{
			MergeIntoConvexPieces(Out.Pieces, Out.VertexPool);
		}

		if (Out.Pieces.Num() > Params.MaxConvexPieces)
		{
			Out.Status = EDecomposeResult::TooManyPieces;
			return Out;
		}

		Out.Status = EDecomposeResult::Success;
		return Out;
	}

	bool TryDecomposeGroup(
		const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
		const TSharedPtr<PCGExClipper2::FOpData>& AllOpData,
		const FDecomposeParams& Params,
		FDecomposeResult& OutResult)
	{
		OutResult = FDecomposeResult(); // Status == Empty (silent skip) by default

		if (!Group->IsValid() || Group->SubjectPaths.empty() || Group->SubjectIndices.IsEmpty())
		{
			return false;
		}

		// SubjectIndices[0] indexes the parallel AllOpData arrays; one check covers all later lookups.
		const int32 FrameSrcIdx = Group->SubjectIndices[0];
		if (!AllOpData->Projections.IsValidIndex(FrameSrcIdx) || !AllOpData->Facades.IsValidIndex(FrameSrcIdx))
		{
			return false;
		}

		OutResult = Decompose(Group->SubjectPaths, AllOpData, Group->CreateZCallback(), Params);
		return OutResult.Status == EDecomposeResult::Success;
	}

	FText DescribeDecomposeFailure(const FDecomposeResult& Result, const FText& Subject, const int32 MaxConvexPieces)
	{
		switch (Result.Status)
		{
		case EDecomposeResult::TriangulationFailed:
			return FText::Format(
				LOCTEXT("TriangulationFailed", "A {0} could not be triangulated (degenerate or self-intersecting) and was skipped."),
				Subject);
		case EDecomposeResult::TooManyPieces:
			return FText::Format(
				LOCTEXT("TooManyPieces", "A {0} needs {1} convex pieces (over the {2} cap) and was skipped. Raise Max Convex Pieces or simplify the path."),
				Subject, FText::AsNumber(Result.Pieces.Num()), FText::AsNumber(MaxConvexPieces));
		default:
			return FText::GetEmpty(); // Success/Empty: nothing to report (Empty is a silent skip)
		}
	}
}

#undef LOCTEXT_NAMESPACE
