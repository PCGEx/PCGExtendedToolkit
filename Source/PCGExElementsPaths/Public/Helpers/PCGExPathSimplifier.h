// Copyright (c) Nebukam

#pragma once

#include "CoreMinimal.h"
#include "Utils/PCGValueRange.h"

#include "PCGExPathSimplifier.generated.h"

UENUM(BlueprintType)
enum class EPCGExTangentSmoothing : uint8
{
	None UMETA(DisplayName = "None (Accurate)", ToolTip="Keep tangents separate - most accurate to original curve"),
	DirectionOnly UMETA(DisplayName = "Direction Only (G1)", ToolTip="Blend directions only, re-optimize magnitudes"),
	Full UMETA(DisplayName = "Full (C1)", ToolTip="Fully matched tangents with optimized magnitudes")
};

namespace PCGExPaths
{
	struct FSimplifiedPoint
	{
		FTransform Transform = FTransform::Identity;
		FVector TangentIn = FVector::ZeroVector;
		FVector TangentOut = FVector::ZeroVector;
		int8 bIsRemovable = false;
		int32 OriginalIndex = -1;

		FSimplifiedPoint() = default;
		FSimplifiedPoint(const FTransform& InTransform, int8 bCanRemove = true);
	};

	class FCurveSimplifier
	{
	public:
		/**
		 * Simplify a polyline using Douglas-Peucker, then fit tangents.
		 */
		static TArray<FSimplifiedPoint> SimplifyPolyline(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int8>& InRemovableFlags,
			double MaxError = 0.1,
			bool bIsClosed = false,
			double Smoothing = 0.0,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

		/**
		 * Simplify a polyline with per-point smoothing control.
		 */
		static TArray<FSimplifiedPoint> SimplifyPolyline(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int8>& InRemovableFlags,
			const TArray<double>& InSmoothingValues,
			double MaxError = 0.1,
			bool bIsClosed = false,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

		//~~ Tangent Fitting Only (no simplification) ~~//

		/**
		 * Fit tangents to a pre-selected subset of points.
		 * No simplification is performed - the provided indices define exactly which points to keep.
		 * 
		 * @param InPoints          The original point transforms
		 * @param InSelectedIndices Sorted indices of points to keep (must be sorted ascending)
		 * @param bIsClosed         Whether this is a closed loop
		 * @param Smoothing         How much to blend tangents at junctions (0 = precise, 1 = smooth)
		 * @param SmoothingMode     Type of smoothing to apply
		 */
		static TArray<FSimplifiedPoint> FitTangentsToSelection(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int32>& InSelectedIndices,
			bool bIsClosed = false,
			double Smoothing = 0.0,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

		/**
		 * Fit tangents to a pre-selected subset with per-point smoothing.
		 * 
		 * @param InPoints              The original point transforms
		 * @param InSelectedIndices     Sorted indices of points to keep
		 * @param InSmoothingValues     Per-point smoothing (indexed by original point index, same size as InPoints)
		 * @param bIsClosed             Whether this is a closed loop
		 * @param SmoothingMode         Type of smoothing to apply
		 */
		static TArray<FSimplifiedPoint> FitTangentsToSelection(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int32>& InSelectedIndices,
			const TArray<double>& InSmoothingValues,
			bool bIsClosed = false,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

		/**
		 * Fit tangents using a keep/remove mask.
		 * Points where InKeepFlags[i] == true will be kept.
		 * 
		 * @param InPoints          The original point transforms
		 * @param InKeepFlags       Which points to keep (true = keep, false = remove)
		 * @param bIsClosed         Whether this is a closed loop
		 * @param Smoothing         Uniform smoothing value
		 * @param SmoothingMode     Type of smoothing to apply
		 */
		static TArray<FSimplifiedPoint> FitTangentsToMask(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int8>& InKeepFlags,
			bool bIsClosed = false,
			double Smoothing = 0.0,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

		/**
		 * Fit tangents using a keep/remove mask with per-point smoothing.
		 */
		static TArray<FSimplifiedPoint> FitTangentsToMask(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int8>& InKeepFlags,
			const TArray<double>& InSmoothingValues,
			bool bIsClosed = false,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

		/**
		 * Fit tangents to ALL points (no reduction at all).
		 * Useful when you just want tangent computation for an existing polyline.
		 */
		static TArray<FSimplifiedPoint> FitTangentsToAll(
			const TConstPCGValueRange<FTransform>& InPoints,
			bool bIsClosed = false,
			double Smoothing = 0.0,
			EPCGExTangentSmoothing SmoothingMode = EPCGExTangentSmoothing::Full);

	private:
		// Core implementation for simplification path
		static TArray<FSimplifiedPoint> SimplifyPolylineInternal(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int8>& InRemovableFlags,
			const TArray<double>& InSmoothingValues,
			double UniformSmoothing,
			double MaxError,
			bool bIsClosed,
			EPCGExTangentSmoothing SmoothingMode);

		// Core implementation for selection-based fitting
		static TArray<FSimplifiedPoint> FitTangentsToSelectionInternal(
			const TConstPCGValueRange<FTransform>& InPoints,
			const TArray<int32>& InSelectedIndices,
			const TArray<double>& InSmoothingValues,
			double UniformSmoothing,
			bool bIsClosed,
			EPCGExTangentSmoothing SmoothingMode);

		// Douglas-Peucker
		static TArray<int32> SimplifyWithDP(
			const TConstPCGValueRange<FTransform>& Points,
			const TArray<int8>& RemovableFlags,
			double MaxError,
			bool bIsClosed);

		// EndIndex may be Points.Num(), the closed-loop virtual chain's terminal index, which resolves to point 0.
		static void SimplifyRecursive(
			const TConstPCGValueRange<FTransform>& Points,
			const TArray<int8>& RemovableFlags,
			TArray<int32>& SelectedIndices,
			int32 StartIndex,
			int32 EndIndex,
			double MaxError);

		static double PointToLineDistance(const FVector& Point, const FVector& LineStart, const FVector& LineEnd);

		// Tangent fitting
		static void FitTangentsLeastSquares(
			TArray<FSimplifiedPoint>& SimplifiedPoints,
			const TConstPCGValueRange<FTransform>& OriginalPoints,
			bool bIsClosed);

		static void SmoothAndRefitTangents(
			TArray<FSimplifiedPoint>& SimplifiedPoints,
			const TConstPCGValueRange<FTransform>& OriginalPoints,
			bool bIsClosed,
			const TArray<double>& InSmoothingValues,
			double UniformSmoothing,
			EPCGExTangentSmoothing SmoothingMode);

		static FVector ComputeSmoothedDirection(
			const FVector& TangentIn,
			const FVector& TangentOut,
			double Smoothing);

		static void FitSegmentMagnitudes(
			const FVector& P0Pos,
			const FVector& P1Pos,
			const FVector& Dir0,
			const FVector& Dir1,
			const TArray<FVector>& IntermediatePoints,
			const TArray<double>& TValues,
			double& OutMag0,
			double& OutMag1);

		static void FitSegmentTangentsLS(
			const FVector& P0Pos,
			const FVector& P1Pos,
			const TArray<FVector>& IntermediatePoints,
			const TArray<double>& TValues,
			FVector& OutT0,
			FVector& OutT1);

		static void ComputeChordLengthParams(
			const FVector& P0,
			const FVector& P1,
			const TArray<FVector>& IntermediatePoints,
			TArray<double>& OutTValues);

		static void GetIntermediatePoints(
			const TConstPCGValueRange<FTransform>& OriginalPoints,
			int32 StartIdx,
			int32 EndIdx,
			int32 TotalPoints,
			bool bIsClosed,
			TArray<FVector>& OutPoints);

		FORCEINLINE static double H00(double t)
		{
			return 2.0 * t * t * t - 3.0 * t * t + 1.0;
		}

		FORCEINLINE static double H10(double t)
		{
			return t * t * t - 2.0 * t * t + t;
		}

		FORCEINLINE static double H01(double t)
		{
			return -2.0 * t * t * t + 3.0 * t * t;
		}

		FORCEINLINE static double H11(double t)
		{
			return t * t * t - t * t;
		}

		static FVector EvaluateHermite(const FVector& P0, const FVector& T0,
		                               const FVector& P1, const FVector& T1, double t);
	};
}
