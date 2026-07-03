// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExPathProfile.generated.h"

struct FPCGExContext;
struct FPCGExManhattanDetails;

namespace PCGExData
{
	class FFacade;
}

namespace PCGExMath::Geo
{
	struct FExCenterArc;
}

UENUM()
enum class EPCGExPathProfileScaling : uint8
{
	Uniform  = 0 UMETA(DisplayName = "Uniform", ToolTip="Keep the profile ratio uniform (relative to the reference length)."),
	Scale    = 1 UMETA(DisplayName = "Scale", ToolTip="Use a scale factor relative to the reference length."),
	Distance = 2 UMETA(DisplayName = "Distance", ToolTip="Use a fixed distance."),
};

/**
 * Shared profile-subdivision routines used by path nodes that carve a segment into a
 * Line / Arc / Custom / Manhattan profile (e.g. Path : Bevel, Path : Extrude).
 *
 * Every routine is settings-agnostic: callers resolve their own values (counts, scaling,
 * plane normal, arc) and feed raw geometry in. Each routine fully (re)fills Out.
 */
namespace PCGExPaths::Profile
{
	/**
	 * Straight subdivision of the segment [A -> B].
	 * Resets Out then fills it with the interior subdivision points in A->B order (endpoints excluded).
	 * @param Factor Subdivision count (when bIsCount) or spacing distance (otherwise).
	 */
	PCGEXELEMENTSPATHS_API void SubdivideLine(
		TArray<FVector>& Out, const FVector& A, const FVector& B, const double Factor, const bool bIsCount);

	/**
	 * Straight subdivision routed through Corner: [A -> Corner -> B], keeping Corner as a mid point.
	 * Step size is derived from the A->Corner leg and mirrored on the Corner->B leg (bevel-symmetric behavior).
	 */
	PCGEXELEMENTSPATHS_API void SubdivideLineKeepCorner(
		TArray<FVector>& Out, const FVector& A, const FVector& Corner, const FVector& B, const double Factor, const bool bIsCount);

	/**
	 * Subdivision along a pre-built arc (the caller constructs it however it likes).
	 * Falls back to SubdivideLine(A, B) when the arc is degenerate (bIsLine).
	 */
	PCGEXELEMENTSPATHS_API void SubdivideArc(
		TArray<FVector>& Out, const PCGExMath::Geo::FExCenterArc& Arc,
		const FVector& A, const FVector& B, const double Factor, const bool bIsCount);

	/**
	 * Custom profile projected between A and B, in the frame whose Z is PlaneNormal and X is (B-A).
	 * ProfilePositions are expected normalized (X in 0..1 along the profile length, Y/Z lateral); the
	 * first and last positions are treated as the profile anchors and skipped.
	 * @param MainAxisSize  Scale applied to the profile's Y axis.
	 * @param CrossAxisSize Scale applied to the profile's Z axis (out of plane).
	 */
	PCGEXELEMENTSPATHS_API void SubdivideCustom(
		TArray<FVector>& Out, const TArray<FVector>& ProfilePositions,
		const FVector& A, const FVector& B, const FVector& PlaneNormal,
		const double MainAxisSize, const double CrossAxisSize);

	/**
	 * Manhattan subdivision of [A -> B], optionally routed through Corner (kept as a mid point when non-null).
	 */
	PCGEXELEMENTSPATHS_API void SubdivideManhattan(
		TArray<FVector>& Out, const FPCGExManhattanDetails& Details, const int32 Index,
		const FVector& A, const FVector& B, const FVector* Corner = nullptr);

	/**
	 * Resolves a profile axis size from its scaling mode.
	 * @param UniformSize    Size used as-is for Uniform.
	 * @param ScaleReference Length the Scale mode multiplies (may differ from UniformSize, e.g. bevel depth vs span).
	 */
	PCGEXELEMENTSPATHS_API double ResolveAxisSize(
		const EPCGExPathProfileScaling Scaling, const double Scale,
		const double UniformSize, const double ScaleReference);

	/**
	 * Fetches the single-path custom profile input from InPinLabel, validates it (at least two points,
	 * non-overlapping endpoints) and normalizes its positions into OutPositions: first point at origin,
	 * X running 0..1 along the profile span. Logs errors on the context and returns false when invalid.
	 */
	PCGEXELEMENTSPATHS_API bool TryBuildCustomProfile(
		FPCGExContext* InContext, const FName InPinLabel,
		TSharedPtr<PCGExData::FFacade>& OutFacade, TArray<FVector>& OutPositions);
}
