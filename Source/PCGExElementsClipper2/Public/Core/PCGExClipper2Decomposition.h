// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clipper2Lib/clipper.h"
#include "Core/PCGExClipper2Processor.h"

// Shared geometry pipeline for the Clipper2 geometry nodes (Volume, Decompose):
// projected closed paths -> triangulation -> deduplicated vertex pool -> Hertel-Mehlhorn convex pieces.
namespace PCGExClipper2Decomposition
{
	/** A deduplicated 2D footprint vertex. Source-backed verts map into FOpData; Clipper-created verts (bHasSource == false) are positioned by unprojecting Pos with ProjectedZ. */
	struct FFootprintVertex
	{
		// Projection-space 2D position (Clipper2 ints / Precision).
		FVector2D Pos = FVector2D::ZeroVector;

		// Projection-space Z of the source point (0 if none).
		double ProjectedZ = 0;

		// FOpData facade index of the source, or INDEX_NONE.
		int32 SourceIdx = INDEX_NONE;

		// Point index within the source, or INDEX_NONE.
		int32 SourcePointIdx = INDEX_NONE;

		bool bHasSource = false;
	};

	/** Outcome of a decomposition pass. */
	enum class EDecomposeResult : uint8
	{
		// Decomposition produced usable convex pieces within the cap.
		Success,
		// Clipper2 triangulation returned a non-success code or produced no triangles.
		TriangulationFailed,
		// Triangulation succeeded but no usable pieces / vertices remained after dedup.
		Empty,
		// Piece count exceeded MaxConvexPieces (Pieces is still populated, for reporting the count).
		TooManyPieces,
	};

	/** Inputs that select how the footprint is triangulated and merged. */
	struct FDecomposeParams
	{
		// Decimal precision used to scale Clipper2 int coordinates back to float (mirrors the node setting).
		int32 Precision = 100;

		// Fill rule for the triangulation (Even-Odd treats nested rings as holes).
		EPCGExClipper2FillRule FillRule = EPCGExClipper2FillRule::EvenOdd;

		// Use Delaunay refinement during triangulation.
		bool bUseDelaunay = true;

		// Hertel-Mehlhorn merge of triangles into convex pieces; false keeps triangles.
		bool bMergeConvexPieces = true;

		// Safety cap on convex pieces; exceeding it yields EDecomposeResult::TooManyPieces.
		int32 MaxConvexPieces = 256;
	};

	/** Build params from any settings exposing the decompose fields (templated -- Volume and Decompose share no common base). */
	template <typename TSettings>
	FDecomposeParams MakeParams(const TSettings* Settings)
	{
		FDecomposeParams Params;
		Params.Precision = Settings->Precision;
		Params.FillRule = Settings->FillRule;
		Params.bUseDelaunay = true;
		Params.bMergeConvexPieces = Settings->bMergeConvexPieces;
		Params.MaxConvexPieces = Settings->MaxConvexPieces;
		return Params;
	}

	/** Result of a decomposition pass: the vertex pool, the convex piece loops, and a status. */
	struct FDecomposeResult
	{
		// Deduplicated vertex pool shared by every piece.
		TArray<FFootprintVertex> VertexPool;

		// Convex piece loops, CCW, each entry an index into VertexPool.
		TArray<TArray<int32>> Pieces;

		// Outcome; only Success guarantees Pieces/VertexPool are ready for downstream use.
		EDecomposeResult Status = EDecomposeResult::Empty;
	};

	/** Decompose one group's projected closed paths (Clipper2 int space, Point64.z = H64(PointIndex, SourceIndex)). Does NOT log -- inspect the Status. */
	PCGEXELEMENTSCLIPPER2_API FDecomposeResult Decompose(
		const PCGExClipper2Lib::Paths64& SubjectPaths,
		const TSharedPtr<PCGExClipper2::FOpData>& AllOpData,
		const PCGExClipper2Lib::ZCallback64& ZCallback,
		const FDecomposeParams& Params);

	/** Validate a group, resolve its frame subject (SubjectIndices[0]), and run Decompose(). True only on Success; otherwise OutResult.Status reports why. Does NOT log. */
	PCGEXELEMENTSCLIPPER2_API bool TryDecomposeGroup(
		const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group,
		const TSharedPtr<PCGExClipper2::FOpData>& AllOpData,
		const FDecomposeParams& Params,
		FDecomposeResult& OutResult);

	/** Warning text for a failed decomposition, keyed on the subject noun ("volume"/"footprint"). Empty for Success/Empty (silent skip). */
	PCGEXELEMENTSCLIPPER2_API FText DescribeDecomposeFailure(const FDecomposeResult& Result, const FText& Subject, int32 MaxConvexPieces);
}
