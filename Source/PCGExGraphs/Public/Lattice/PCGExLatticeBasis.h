// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/**
 * Runtime lattice basis derived from an ordered step set (directions + length multipliers) and a base
 * cell size.
 *
 * The step directions span a global periodic lattice: the SHORTEST independent subset of them becomes
 * the lattice AXES (1..3) -- shortest because those are the steps that GENERATE the lattice -- and every
 * step, axis or diagonal, resolves to an integer coordinate OFFSET in that basis. Consumers snap world
 * points to coordinates and connect neighbours by adding those offsets.
 *
 * The linear solve runs in the dimensionless UNIT-direction basis with the per-axis lengths applied
 * separately, so snapping stays robust from tiny to huge cell sizes without a scale-dependent
 * singularity threshold.
 *
 * Holds no UObject references, so it is safe to read off the game thread. Build once, query freely.
 */
struct PCGEXGRAPHS_API FPCGExLatticeBasis
{
	/** Lattice origin in world space (a lattice node sits exactly here). */
	FVector Origin = FVector::ZeroVector;

	/**
	 * The rigid frame rotation BuildFromSteps applied to every step direction. Consumers emitting points
	 * for lattice nodes must carry this as the point rotation, or anything re-deriving directions from
	 * a point transform resolves the authored, un-rotated set and disagrees with the tags written.
	 */
	FQuat FrameRotation = FQuat::Identity;

	/** Base cell size (lattice spacing) the step length multipliers multiply. */
	double CellSize = 100.0;

	/** Step count the per-step arrays below are sized to. */
	int32 NumSteps = 0;

	/** Per-step normalized world direction exactly as authored (frame rotation applied) -- the step's
	 *  identity, never snapped. [NumSteps] */
	TArray<FVector> StepDirs;

	/**
	 * Per-step world vector. The authored Dir * LengthMultiplier * CellSize while UNRESOLVED; once
	 * resolved it is the EXACT lattice vector of its offset, so a diagonal authored as 1.41 rather
	 * than 1.4142135624 doesn't leave a step 0.3% short of the very node its offset names.
	 * Invariant: StepOffsets[i] != 0 => StepVectors[i] == CoordToWorld(StepOffsets[i]) - Origin.
	 * [NumSteps]
	 */
	TArray<FVector> StepVectors;

	/**
	 * Per-step offset in integer lattice coordinates (its neighbour offset). Zero when the step does
	 * not land on a lattice node -- consumers skip those. [NumSteps]
	 */
	TArray<FIntVector> StepOffsets;

	/** Steps that resolved to no offset. Non-empty means the step set and the cell size disagree; report
	 *  them rather than silently drop those directions. Indices into the input arrays, so the caller can
	 *  format labels from whatever authored the steps. */
	TArray<int32> UnresolvedSteps;

	/**
	 * The legal single-step lattice moves: every resolved step offset AND its negation (the lattice is
	 * a group and edges are undirected, so stepping against a step still lands on a node it connects),
	 * plus the axis unit offsets -- generators by construction, so any coordinate stays reachable. Deduped,
	 * deterministic order (axes first, then step index order).
	 *
	 * Consumers that WALK the lattice must move through these rather than the axes alone: the axes are an
	 * arbitrary basis choice, so restricting moves to them biases a path onto the basis instead of the
	 * directions the step set declares.
	 */
	TArray<FIntVector> WalkOffsets;

	/** Number of independent lattice axes (1..3). */
	int32 NumAxes = 0;

	/** World vector of each axis (already scaled by LengthMultiplier * CellSize). [0..NumAxes) */
	FVector AxisVecs[3] = {};

	/** Step index each axis was derived from (its +offset is the k-th unit coordinate). [0..NumAxes) */
	int32 AxisStep[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};

	/** True once BuildFromSteps succeeded with >= 1 axis. */
	bool bValid = false;

	/**
	 * Build the basis from an ordered step set. InDirections need not be normalized (they are normalized
	 * here); each step's world length = InLengthMultipliers[i] * InCellSize. The two arrays must be the
	 * same size.
	 * @return true if at least one independent axis was found.
	 */
	bool BuildFromSteps(TConstArrayView<FVector> InDirections, TConstArrayView<double> InLengthMultipliers, double InCellSize, const FVector& InOrigin = FVector::ZeroVector, const FQuat& InFrameRot = FQuat::Identity);

	/** Snap a world position to its TRUE nearest integer lattice coordinate: Babai rounding corrected by a
	 *  fixed local search, which matters on any skewed (hex / triangular) basis where the rounded
	 *  coordinate is not the nearest node. Exact on an orthogonal basis, where rounding already is.
	 *  Components beyond NumAxes come back 0 -- callers re-snapping an EXISTING coord must use the
	 *  preserving variant below, or a rank-collapsed basis wipes the stashed components. */
	FIntVector SnapWorldToCoord(const FVector& World) const;

	/**
	 * SnapWorldToCoord, but unspanned components (>= NumAxes) keep Previous's values instead of 0.
	 * A rank-deficient basis PROJECTS coords rather than truncating them -- CoordToWorld ignores the
	 * unspanned components -- so a re-snap through this variant is idempotent at any rank and the
	 * hidden components survive until their axis returns.
	 */
	FIntVector SnapWorldToCoordPreserving(const FVector& World, const FIntVector& Previous) const;

	/** Continuous (un-rounded) lattice coordinate of a world position; useful for interpolation.
	 *  Components beyond NumAxes are 0. */
	FVector ContinuousCoord(const FVector& World) const;

	/** Exact world position of an integer lattice coordinate. */
	FVector CoordToWorld(const FIntVector& Coord) const;

	/** Unit coordinate offset of axis k (the k-th basis vector), e.g. (1,0,0). Zero if out of range. */
	FIntVector AxisUnitOffset(int32 AxisIndex) const;

	/**
	 * Coord-space half-extent (per axis) whose axis-aligned box provably contains every lattice
	 * node within WorldRadius of a given node, accounting for basis skew via the dual-basis norm.
	 * Components beyond NumAxes are 0. Used to bound brush/thickness enumeration.
	 */
	FIntVector CoordExtentForWorldRadius(double WorldRadius) const;

	/**
	 * Orthonormal basis of the subspace the lattice axes do NOT span (the plane normal for a
	 * 2-axis lattice, two directions for a 1-axis one, nothing for a 3-axis one). Snapping discards
	 * any displacement along these, so consumers that need to place the frame relative to source
	 * data measure it here. Writes up to (3 - NumAxes) vectors; returns how many.
	 */
	int32 GetComplementBasis(FVector OutDirs[3]) const;

private:
	/** Normalized axis directions (the scale-free solve basis). [0..NumAxes) */
	FVector UnitAxis[3] = {};

	/** Axis lengths = |AxisVecs[k]| = LengthMultiplier * CellSize. Always > 0. [0..NumAxes) */
	double AxisLen[3] = {};

	/** Inverse of the unit Gram matrix (UnitAxis^T UnitAxis), dimensionless. Upper-left NumAxes^2 used. */
	double UnitGramInv[3][3] = {};

	/** Fill UnitGramInv from UnitAxis for the current NumAxes. Returns false if singular. */
	bool ComputeUnitGramInverse();

	/** Continuous lattice coords of a world DISPLACEMENT (already Origin-relative). Writes 3 doubles. */
	void ComputeContinuousLocal(const FVector& Displacement, double OutX[3]) const;
};
