// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Lattice/PCGExLatticeBasis.h"

#include "PCGExLatticePaint.generated.h"

namespace PCGExPaths
{
	class FPolyPath;
}

/** How a lattice frame's base is inferred along the directions the lattice doesn't span. */
UENUM(BlueprintType)
enum class EPCGExLatticeBaseMode : uint8
{
	Flat    = 0 UMETA(ToolTip = "No inference -- the frame sits exactly at Origin."),
	Min     = 1 UMETA(ToolTip = "Base at the lowest input position along the unspanned directions."),
	Max     = 2 UMETA(ToolTip = "Base at the highest input position along the unspanned directions."),
	Average = 3 UMETA(ToolTip = "Base at the average input position along the unspanned directions."),
};

/** Lattice painting: quantize shapes onto a FPCGExLatticeBasis as coord sets (spines, fills, holes). */
namespace PCGExLattice
{
	// Hard ceiling on a merged coord set -- a runaway guard against a tiny cell size relative to the
	// input extent. Not a real limit for sane configs. Cumulative per shared OutCoords set: consumers
	// painting several shapes into one set get one ceiling for the union.
	constexpr int32 MaxLatticeNodes = 4000000;

	// Companion ceiling on cells VISITED. MaxLatticeNodes counts ACCEPTED cells, so it never trips
	// when acceptance is sparse or zero -- the sweep runs to completion regardless. Per coord box.
	constexpr int64 MaxLatticeCellVisits = 16000000;

	// Largest brush half-extent (per axis) a single spine dilation may enumerate. A radius that needs
	// more than this should be refused up-front rather than silently painting a box-clipped ball.
	constexpr int32 MaxBrushHalfExtent = 78; // (2*78+1)^3 ~= 3.87M < MaxLatticeNodes

	/** Each span widens to int64 BEFORE the subtract: coords reach +-MAX_int32/2, so subtracting in
	 *  int32 wraps a full-range span negative and every budget check on the result silently passes. */
	FORCEINLINE int64 CoordBoxCellCount(const FIntVector& MinC, const FIntVector& MaxC)
	{
		return (static_cast<int64>(MaxC.X) - static_cast<int64>(MinC.X) + 1) *
			(static_cast<int64>(MaxC.Y) - static_cast<int64>(MinC.Y) + 1) *
			(static_cast<int64>(MaxC.Z) - static_cast<int64>(MinC.Z) + 1);
	}

	/** Coord-space L1 distance -- StairFill's progress measure, and the bound on its step count. */
	FORCEINLINE int32 CoordL1(const FIntVector& V)
	{
		return FMath::Abs(V.X) + FMath::Abs(V.Y) + FMath::Abs(V.Z);
	}

	/**
	 * Walk the lattice from A to B one legal step at a time, adding every coord passed to OutCoords
	 * and OutSpine.
	 *
	 * Steps come from `Basis.WalkOffsets` -- every resolved step offset and its negation -- NOT the
	 * axes alone, which are an arbitrary basis choice that bends any path along a non-axis step: a
	 * straight diagonal on a compass set takes 13 nodes zigzagging 0.707 cells off the line where 7 sit
	 * exactly on it, and every surplus node picks up its own full edge set.
	 *
	 * Among the steps that strictly close the coord-space gap (which bounds the walk and guarantees
	 * arrival), the winner lands nearest the A->B world line -- fidelity to the segment, not the basis.
	 * Larger coord progress breaks ties, then candidate order, keeping the walk deterministic.
	 */
	PCGEXGRAPHS_API void StairFill(const FPCGExLatticeBasis& Basis, const FIntVector& A, const FIntVector& B, TSet<FIntVector>& OutCoords, TArray<FIntVector>& OutSpine);

	/**
	 * Paint one shape onto the lattice as a linear spine. Snap each vertex + stair-fill between
	 * consecutive vertices -> connected 1-wide spine; then dilate the spine by CaptureRadius (world
	 * distance) -> thickness. Radius 0 = spine only.
	 */
	PCGEXGRAPHS_API void PaintLinear(const PCGExPaths::FPolyPath& Path, const FPCGExLatticeBasis& Basis, const double CaptureRadius, bool bForceClosed, TSet<FIntVector>& OutCoords);

	/** Half the smallest lattice cell dimension -- the inclusion epsilon used by containment tests. */
	PCGEXGRAPHS_API double HalfCellEpsilon(const FPCGExLatticeBasis& Basis);

	/**
	 * Twice-signed area of the projected outline, halved: positive for counter-clockwise winding,
	 * negative for clockwise. Used to pick the sign that makes OffsetProjection GROW the polygon,
	 * since its shift direction is winding-dependent.
	 */
	PCGEXGRAPHS_API double ProjectedSignedArea(const PCGExPaths::FPolyPath& Path);

	/**
	 * Coord-space AABB of the candidate lattice nodes for a world box. All 8 corners are snapped because
	 * a rotated/skew lattice doesn't preserve which corner is extremal.
	 *
	 * ProjectionNormal, when set, names the plane a 2D containment test projects onto: padding along an
	 * axis parallel to it only adds cells that test can never reject, so a flat shape on a 3-axis
	 * lattice would inflate into a slab.
	 */
	PCGEXGRAPHS_API bool WorldBoundsToCoordBox(const FBox& WorldBounds, const FPCGExLatticeBasis& Basis, const double PadWorld, FIntVector& OutMin, FIntVector& OutMax,
	                                           const FVector* ProjectionNormal = nullptr);

	/**
	 * Fill a closed SOLID shape's interior. Containment tests the outline AS GIVEN -- callers wanting
	 * the half-cell grow/erode behavior outset the projected outline BEFORE calling (OffsetProjection,
	 * signed by ProjectedSignedArea).
	 *
	 * The outline fallback catches a shape thinner than one cell, which would otherwise vanish -- but
	 * not when the radius erodes (negative CaptureRadius), where an empty result is the point. Open
	 * shapes degrade to their outline; holes subtract separately, in SubtractHole.
	 *
	 * False = coord box over the cell-visit budget; caller reports + aborts.
	 */
	PCGEXGRAPHS_API bool PaintSurface(const PCGExPaths::FPolyPath& Path, const FPCGExLatticeBasis& Basis, const double CaptureRadius, bool bForceClosed, TSet<FIntVector>& OutCoords);

	/**
	 * Subtract a closed HOLE shape from an already-filled union. The outline should be INSET at build
	 * time, mirroring a solid's half-cell grow, so radius keeps meaning "this shape's influence extent"
	 * on both sides: positive enlarges the hole, negative shrinks it.
	 *
	 * Holes are explicit, NOT even/odd nesting -- the fill is (union of solids) minus (union of holes),
	 * so a solid nested inside a hole is removed. Arbitrary nesting would need an even/odd pass.
	 *
	 * False = coord box over the cell-visit budget; caller reports + aborts.
	 */
	PCGEXGRAPHS_API bool SubtractHole(const PCGExPaths::FPolyPath& Path, const FPCGExLatticeBasis& Basis, const double CaptureRadius, TSet<FIntVector>& OutCoords);

	/**
	 * Base offset inferred from the source shapes, measured along the directions the lattice does NOT
	 * span. Snapping discards displacement along those, so a planar lattice would otherwise always sit
	 * on the plane through Origin regardless of where the input data actually is. Zero for Flat, for a
	 * 3-axis lattice (nothing is discarded), or when there is no data.
	 */
	PCGEXGRAPHS_API FVector ComputeDataBaseOffset(const TArray<TSharedPtr<PCGExPaths::FPolyPath>>& Shapes, const TArray<FBox>& VolumeEntryBounds, const FPCGExLatticeBasis& Basis, EPCGExLatticeBaseMode Mode);
}
