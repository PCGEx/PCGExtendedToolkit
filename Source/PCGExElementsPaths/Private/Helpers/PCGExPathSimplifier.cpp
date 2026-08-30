// Copyright 2026 Timothé Lapetite and contributors

#include "Helpers/PCGExPathSimplifier.h"

namespace PCGExPaths
{
	FSimplifiedPoint::FSimplifiedPoint(const FTransform& InTransform, int8 bCanRemove)
		: Transform(InTransform)
		  , bIsRemovable(bCanRemove)
	{
		TangentIn = FVector::ZeroVector;
		TangentOut = FVector::ZeroVector;
	}


	TArray<FSimplifiedPoint> FCurveSimplifier::SimplifyPolyline(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int8>& InRemovableFlags,
		double MaxError,
		bool bIsClosed,
		double Smoothing,
		EPCGExTangentSmoothing SmoothingMode)
	{
		return SimplifyPolylineInternal(
			InPoints, InRemovableFlags,
			TArray<double>(),
			Smoothing,
			MaxError, bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::SimplifyPolyline(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int8>& InRemovableFlags,
		const TArray<double>& InSmoothingValues,
		double MaxError,
		bool bIsClosed,
		EPCGExTangentSmoothing SmoothingMode)
	{
		return SimplifyPolylineInternal(
			InPoints, InRemovableFlags,
			InSmoothingValues,
			0.0,
			MaxError, bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::FitTangentsToSelection(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int32>& InSelectedIndices,
		bool bIsClosed,
		double Smoothing,
		EPCGExTangentSmoothing SmoothingMode)
	{
		return FitTangentsToSelectionInternal(
			InPoints, InSelectedIndices,
			TArray<double>(),
			Smoothing,
			bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::FitTangentsToSelection(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int32>& InSelectedIndices,
		const TArray<double>& InSmoothingValues,
		bool bIsClosed,
		EPCGExTangentSmoothing SmoothingMode)
	{
		return FitTangentsToSelectionInternal(
			InPoints, InSelectedIndices,
			InSmoothingValues,
			0.0,
			bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::FitTangentsToMask(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int8>& InKeepFlags,
		bool bIsClosed,
		double Smoothing,
		EPCGExTangentSmoothing SmoothingMode)
	{
		if (InPoints.Num() != InKeepFlags.Num() || InPoints.Num() < 2)
		{
			return {};
		}

		TArray<int32> SelectedIndices;
		for (int32 i = 0; i < InKeepFlags.Num(); ++i)
		{
			if (InKeepFlags[i])
			{
				SelectedIndices.Add(i);
			}
		}

		return FitTangentsToSelectionInternal(
			InPoints, SelectedIndices,
			TArray<double>(),
			Smoothing,
			bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::FitTangentsToMask(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int8>& InKeepFlags,
		const TArray<double>& InSmoothingValues,
		bool bIsClosed,
		EPCGExTangentSmoothing SmoothingMode)
	{
		if (InPoints.Num() != InKeepFlags.Num() || InPoints.Num() < 2)
		{
			return {};
		}

		TArray<int32> SelectedIndices;
		for (int32 i = 0; i < InKeepFlags.Num(); ++i)
		{
			if (InKeepFlags[i])
			{
				SelectedIndices.Add(i);
			}
		}

		return FitTangentsToSelectionInternal(
			InPoints, SelectedIndices,
			InSmoothingValues,
			0.0,
			bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::FitTangentsToAll(
		const TConstPCGValueRange<FTransform>& InPoints,
		bool bIsClosed,
		double Smoothing,
		EPCGExTangentSmoothing SmoothingMode)
	{
		TArray<int32> AllIndices;
		AllIndices.Reserve(InPoints.Num());
		for (int32 i = 0; i < InPoints.Num(); ++i)
		{
			AllIndices.Add(i);
		}

		return FitTangentsToSelectionInternal(
			InPoints, AllIndices,
			TArray<double>(),
			Smoothing,
			bIsClosed, SmoothingMode);
	}


	TArray<FSimplifiedPoint> FCurveSimplifier::SimplifyPolylineInternal(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int8>& InRemovableFlags,
		const TArray<double>& InSmoothingValues,
		double UniformSmoothing,
		double MaxError,
		bool bIsClosed,
		EPCGExTangentSmoothing SmoothingMode)
	{
		if (InPoints.Num() < 2 || InPoints.Num() != InRemovableFlags.Num())
		{
			return {};
		}

		const bool bHasPerPointSmoothing = InSmoothingValues.Num() > 0;
		if (bHasPerPointSmoothing && InSmoothingValues.Num() != InPoints.Num())
		{
			return {};
		}

		TArray<int32> SelectedIndices = SimplifyWithDP(InPoints, InRemovableFlags, MaxError, bIsClosed);

		return FitTangentsToSelectionInternal(
			InPoints, SelectedIndices,
			InSmoothingValues, UniformSmoothing,
			bIsClosed, SmoothingMode);
	}

	TArray<FSimplifiedPoint> FCurveSimplifier::FitTangentsToSelectionInternal(
		const TConstPCGValueRange<FTransform>& InPoints,
		const TArray<int32>& InSelectedIndices,
		const TArray<double>& InSmoothingValues,
		double UniformSmoothing,
		bool bIsClosed,
		EPCGExTangentSmoothing SmoothingMode)
	{
		if (InPoints.Num() < 2)
		{
			return {};
		}

		const bool bHasPerPointSmoothing = InSmoothingValues.Num() > 0;
		if (bHasPerPointSmoothing && InSmoothingValues.Num() != InPoints.Num())
		{
			return {};
		}

		// Clean up indices: validate, dedupe, and sort
		TArray<int32> CleanIndices;
		CleanIndices.Reserve(InSelectedIndices.Num());
		for (int32 Idx : InSelectedIndices)
		{
			if (Idx >= 0 && Idx < InPoints.Num())
			{
				CleanIndices.AddUnique(Idx);
			}
		}
		CleanIndices.Sort();

		if (CleanIndices.Num() < 2)
		{
			return {};
		}

		TArray<FSimplifiedPoint> Result;
		Result.Reserve(CleanIndices.Num());

		for (int32 Idx : CleanIndices)
		{
			Result.Add(FSimplifiedPoint(InPoints[Idx], false));
			Result.Last().OriginalIndex = Idx;
		}

		FitTangentsLeastSquares(Result, InPoints, bIsClosed);

		if (SmoothingMode != EPCGExTangentSmoothing::None)
		{
			SmoothAndRefitTangents(Result, InPoints, bIsClosed, InSmoothingValues, UniformSmoothing, SmoothingMode);
		}

		return Result;
	}

	// Douglas-Peucker

	TArray<int32> FCurveSimplifier::SimplifyWithDP(
		const TConstPCGValueRange<FTransform>& Points,
		const TArray<int8>& RemovableFlags,
		double MaxError,
		bool bIsClosed)
	{
		const int32 NumPoints = Points.Num();

		TArray<int32> ResultIndices;

		if (NumPoints < 3)
		{
			for (int32 i = 0; i < NumPoints; ++i)
			{
				ResultIndices.Add(i);
			}
			return ResultIndices;
		}

		// Closed loops are walked as the virtual open chain 0..N-1..0, so the wrap edge is a DP span like any other
		// and point 0 is the only index pinned. Both recursion anchors must survive or their spans go unbounded.
		const int32 TerminalIndex = bIsClosed ? NumPoints : NumPoints - 1;

		ResultIndices.Add(0);
		if (!bIsClosed)
		{
			ResultIndices.Add(NumPoints - 1);
		}

		for (int32 i = 1; i < NumPoints; ++i)
		{
			if (!RemovableFlags[i])
			{
				ResultIndices.AddUnique(i);
			}
		}

		SimplifyRecursive(Points, RemovableFlags, ResultIndices, 0, TerminalIndex, MaxError);

		ResultIndices.Sort();
		for (int32 i = ResultIndices.Num() - 1; i > 0; --i)
		{
			if (ResultIndices[i] == ResultIndices[i - 1])
			{
				ResultIndices.RemoveAt(i);
			}
		}

		return ResultIndices;
	}

	void FCurveSimplifier::SimplifyRecursive(
		const TConstPCGValueRange<FTransform>& Points,
		const TArray<int8>& RemovableFlags,
		TArray<int32>& SelectedIndices,
		int32 StartIndex,
		int32 EndIndex,
		double MaxError)
	{
		if (EndIndex - StartIndex <= 1)
		{
			return;
		}

		double MaxDistance = 0.0;
		int32 MaxIndex = -1;

		// Only EndIndex can be the virtual terminal; StartIndex and every candidate are strictly below it.
		FVector StartPoint = Points[StartIndex].GetLocation();
		FVector EndPoint = Points[EndIndex == Points.Num() ? 0 : EndIndex].GetLocation();

		for (int32 i = StartIndex + 1; i < EndIndex; ++i)
		{
			if (!RemovableFlags[i])
			{
				continue;
			}

			FVector CurrentPoint = Points[i].GetLocation();
			double Distance = PointToLineDistance(CurrentPoint, StartPoint, EndPoint);

			if (Distance > MaxDistance)
			{
				MaxDistance = Distance;
				MaxIndex = i;
			}
		}

		if (MaxDistance > MaxError && MaxIndex != -1)
		{
			SelectedIndices.AddUnique(MaxIndex);
			SimplifyRecursive(Points, RemovableFlags, SelectedIndices, StartIndex, MaxIndex, MaxError);
			SimplifyRecursive(Points, RemovableFlags, SelectedIndices, MaxIndex, EndIndex, MaxError);
		}
	}

	double FCurveSimplifier::PointToLineDistance(const FVector& Point, const FVector& LineStart, const FVector& LineEnd)
	{
		FVector LineDirection = LineEnd - LineStart;
		double LineLength = LineDirection.Size();

		if (FMath::IsNearlyZero(LineLength))
		{
			return (Point - LineStart).Size();
		}

		FVector NormalizedLine = LineDirection.GetSafeNormal();
		FVector VectorToPoint = Point - LineStart;
		double Projection = FVector::DotProduct(VectorToPoint, NormalizedLine);
		Projection = FMath::Clamp(Projection, 0.0, LineLength);

		FVector ClosestPointOnLine = LineStart + NormalizedLine * Projection;
		return (Point - ClosestPointOnLine).Size();
	}

	// Tangent Fitting

	void FCurveSimplifier::FitTangentsLeastSquares(
		TArray<FSimplifiedPoint>& SimplifiedPoints,
		const TConstPCGValueRange<FTransform>& OriginalPoints,
		bool bIsClosed)
	{
		const int32 NumSimplified = SimplifiedPoints.Num();
		if (NumSimplified < 2)
		{
			return;
		}

		const int32 NumOriginal = OriginalPoints.Num();
		const int32 NumSegments = bIsClosed ? NumSimplified : (NumSimplified - 1);

		// Track which segments have intermediate points
		TArray<bool> SegmentHasIntermediates;
		SegmentHasIntermediates.SetNum(NumSegments);

		// First pass: fit tangents per-segment
		for (int32 i = 0; i < NumSegments; ++i)
		{
			const int32 NextIdx = (i + 1) % NumSimplified;

			const int32 OrigStartIdx = SimplifiedPoints[i].OriginalIndex;
			const int32 OrigEndIdx = SimplifiedPoints[NextIdx].OriginalIndex;

			const FVector P0 = SimplifiedPoints[i].Transform.GetLocation();
			const FVector P1 = SimplifiedPoints[NextIdx].Transform.GetLocation();

			TArray<FVector> IntermediatePoints;
			GetIntermediatePoints(OriginalPoints, OrigStartIdx, OrigEndIdx, NumOriginal, bIsClosed, IntermediatePoints);

			SegmentHasIntermediates[i] = IntermediatePoints.Num() > 0;

			TArray<double> TValues;
			ComputeChordLengthParams(P0, P1, IntermediatePoints, TValues);

			FVector T0, T1;
			FitSegmentTangentsLS(P0, P1, IntermediatePoints, TValues, T0, T1);

			SimplifiedPoints[i].TangentOut = T0;
			SimplifiedPoints[NextIdx].TangentIn = T1;
		}

		// Second pass: fix points where segments lack intermediate data
		// Use Catmull-Rom central difference for these
		for (int32 i = 0; i < NumSimplified; ++i)
		{
			int32 PrevSegIdx = -1;
			int32 NextSegIdx = -1;

			if (bIsClosed)
			{
				PrevSegIdx = (i - 1 + NumSimplified) % NumSimplified;
				NextSegIdx = i;
			}
			else
			{
				if (i == 0)
				{
					// First point only has outgoing segment
					SimplifiedPoints[0].TangentIn = SimplifiedPoints[0].TangentOut;
					continue;
				}
				if (i == NumSimplified - 1)
				{
					// Last point only has incoming segment
					SimplifiedPoints[i].TangentOut = SimplifiedPoints[i].TangentIn;
					continue;
				}
				PrevSegIdx = i - 1;
				NextSegIdx = i;
			}

			const bool bPrevHas = SegmentHasIntermediates[PrevSegIdx];
			const bool bNextHas = SegmentHasIntermediates[NextSegIdx];

			// If EITHER segment lacks intermediates, use central difference for smoother results
			if (!bPrevHas || !bNextHas)
			{
				const int32 PrevPtIdx = bIsClosed ? (i - 1 + NumSimplified) % NumSimplified : (i - 1);
				const int32 NextPtIdx = bIsClosed ? (i + 1) % NumSimplified : (i + 1);

				const FVector PrevPos = SimplifiedPoints[PrevPtIdx].Transform.GetLocation();
				const FVector CurrPos = SimplifiedPoints[i].Transform.GetLocation();
				const FVector NextPos = SimplifiedPoints[NextPtIdx].Transform.GetLocation();

				// Compute distances to neighbors
				const double DistToPrev = (CurrPos - PrevPos).Size();
				const double DistToNext = (NextPos - CurrPos).Size();

				// Central difference direction
				FVector CentralDir = (NextPos - PrevPos);
				const double CentralLen = CentralDir.Size();

				if (CentralLen > SMALL_NUMBER)
				{
					CentralDir /= CentralLen;
				}
				else
				{
					CentralDir = (NextPos - CurrPos).GetSafeNormal();
					if (CentralDir.IsNearlyZero())
					{
						CentralDir = (CurrPos - PrevPos).GetSafeNormal();
					}
				}

				// Scale tangent magnitude based on adjacent segment lengths
				// This ensures the Hermite curve properly spans each segment
				const double MagIn = DistToPrev;
				const double MagOut = DistToNext;

				if (!bPrevHas && !bNextHas)
				{
					// Neither segment has data - use scaled central difference
					SimplifiedPoints[i].TangentIn = CentralDir * MagIn;
					SimplifiedPoints[i].TangentOut = CentralDir * MagOut;
				}
				else if (!bPrevHas)
				{
					// Previous segment has no data
					SimplifiedPoints[i].TangentIn = CentralDir * MagIn;
				}
				else // !bNextHas
				{
					// Next segment has no data
					SimplifiedPoints[i].TangentOut = CentralDir * MagOut;
				}
			}
		}

		// Final cleanup for open paths
		if (!bIsClosed)
		{
			SimplifiedPoints[0].TangentIn = SimplifiedPoints[0].TangentOut;
			SimplifiedPoints[NumSimplified - 1].TangentOut = SimplifiedPoints[NumSimplified - 1].TangentIn;
		}
	}

	FVector FCurveSimplifier::ComputeSmoothedDirection(
		const FVector& TangentIn,
		const FVector& TangentOut,
		double Smoothing)
	{
		const double MagIn = TangentIn.Size();
		const double MagOut = TangentOut.Size();

		if (MagIn < SMALL_NUMBER && MagOut < SMALL_NUMBER)
		{
			return FVector::ForwardVector;
		}

		if (MagIn < SMALL_NUMBER)
		{
			return TangentOut.GetSafeNormal();
		}

		if (MagOut < SMALL_NUMBER)
		{
			return TangentIn.GetSafeNormal();
		}

		const FVector DirIn = TangentIn / MagIn;
		const FVector DirOut = TangentOut / MagOut;

		const double DotProduct = FVector::DotProduct(DirIn, DirOut);

		// Reduce smoothing at sharp corners
		const double AngleFactor = FMath::Max(0.0, (DotProduct + 1.0) * 0.5);
		const double EffectiveSmoothing = Smoothing * AngleFactor * AngleFactor;

		FVector AvgDir = DirIn + DirOut;
		const double AvgLen = AvgDir.Size();

		if (AvgLen < SMALL_NUMBER)
		{
			return (MagOut >= MagIn) ? DirOut : DirIn;
		}

		AvgDir /= AvgLen;

		const FVector BlendedIn = FMath::Lerp(DirIn, AvgDir, EffectiveSmoothing);
		const FVector BlendedOut = FMath::Lerp(DirOut, AvgDir, EffectiveSmoothing);

		return ((BlendedIn + BlendedOut) * 0.5).GetSafeNormal();
	}

	void FCurveSimplifier::SmoothAndRefitTangents(
		TArray<FSimplifiedPoint>& SimplifiedPoints,
		const TConstPCGValueRange<FTransform>& OriginalPoints,
		bool bIsClosed,
		const TArray<double>& InSmoothingValues,
		double UniformSmoothing,
		EPCGExTangentSmoothing SmoothingMode)
	{
		const int32 NumPoints = SimplifiedPoints.Num();
		if (NumPoints < 2)
		{
			return;
		}

		const int32 NumOriginal = OriginalPoints.Num();
		const bool bHasPerPointSmoothing = InSmoothingValues.Num() > 0;
		const int32 NumSegments = bIsClosed ? NumPoints : (NumPoints - 1);

		// Step 1: Compute smoothed directions and effective smoothing at each point
		TArray<FVector> SmoothedDirections;
		TArray<double> EffectiveSmoothingValues;
		SmoothedDirections.SetNum(NumPoints);
		EffectiveSmoothingValues.SetNum(NumPoints);

		for (int32 i = 0; i < NumPoints; ++i)
		{
			double Smoothing;
			if (bHasPerPointSmoothing)
			{
				const int32 OrigIdx = SimplifiedPoints[i].OriginalIndex;
				Smoothing = (OrigIdx >= 0 && OrigIdx < InSmoothingValues.Num())
					? FMath::Clamp(InSmoothingValues[OrigIdx], 0.0, 1.0)
					: 0.0;
			}
			else
			{
				Smoothing = FMath::Clamp(UniformSmoothing, 0.0, 1.0);
			}

			if (!bIsClosed && (i == 0 || i == NumPoints - 1))
			{
				const FVector& Tangent = (i == 0) ? SimplifiedPoints[i].TangentOut : SimplifiedPoints[i].TangentIn;
				SmoothedDirections[i] = Tangent.GetSafeNormal();
				EffectiveSmoothingValues[i] = 0.0;
			}
			else
			{
				const FVector& TangentIn = SimplifiedPoints[i].TangentIn;
				const FVector& TangentOut = SimplifiedPoints[i].TangentOut;

				const double MagIn = TangentIn.Size();
				const double MagOut = TangentOut.Size();

				if (MagIn > SMALL_NUMBER && MagOut > SMALL_NUMBER)
				{
					const FVector DirIn = TangentIn / MagIn;
					const FVector DirOut = TangentOut / MagOut;
					const double DotProduct = FVector::DotProduct(DirIn, DirOut);
					const double AngleFactor = FMath::Max(0.0, (DotProduct + 1.0) * 0.5);
					EffectiveSmoothingValues[i] = Smoothing * AngleFactor * AngleFactor;
				}
				else
				{
					EffectiveSmoothingValues[i] = 0.0;
				}

				SmoothedDirections[i] = ComputeSmoothedDirection(TangentIn, TangentOut, Smoothing);
			}
		}

		// Step 2: Re-fit magnitudes for each segment
		TArray<double> MagnitudesOut;
		TArray<double> MagnitudesIn;
		MagnitudesOut.SetNum(NumPoints);
		MagnitudesIn.SetNum(NumPoints);

		// Initialize with original magnitudes
		for (int32 i = 0; i < NumPoints; ++i)
		{
			MagnitudesIn[i] = SimplifiedPoints[i].TangentIn.Size();
			MagnitudesOut[i] = SimplifiedPoints[i].TangentOut.Size();
		}

		for (int32 i = 0; i < NumSegments; ++i)
		{
			const int32 NextIdx = (i + 1) % NumPoints;

			const int32 OrigStartIdx = SimplifiedPoints[i].OriginalIndex;
			const int32 OrigEndIdx = SimplifiedPoints[NextIdx].OriginalIndex;

			const FVector P0 = SimplifiedPoints[i].Transform.GetLocation();
			const FVector P1 = SimplifiedPoints[NextIdx].Transform.GetLocation();

			const FVector Dir0 = SmoothedDirections[i];
			const FVector Dir1 = SmoothedDirections[NextIdx];

			TArray<FVector> IntermediatePoints;
			GetIntermediatePoints(OriginalPoints, OrigStartIdx, OrigEndIdx, NumOriginal, bIsClosed, IntermediatePoints);

			if (IntermediatePoints.Num() > 0)
			{
				TArray<double> TValues;
				ComputeChordLengthParams(P0, P1, IntermediatePoints, TValues);

				double Mag0, Mag1;
				FitSegmentMagnitudes(P0, P1, Dir0, Dir1, IntermediatePoints, TValues, Mag0, Mag1);

				MagnitudesOut[i] = Mag0;
				MagnitudesIn[NextIdx] = Mag1;
			}
		}

		// Step 3: Apply results
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const double EffectiveSmoothing = EffectiveSmoothingValues[i];

			if (EffectiveSmoothing <= SMALL_NUMBER)
			{
				continue;
			}

			if (SmoothingMode == EPCGExTangentSmoothing::Full)
			{
				double AvgMag;
				if (!bIsClosed && i == 0)
				{
					AvgMag = MagnitudesOut[i];
				}
				else if (!bIsClosed && i == NumPoints - 1)
				{
					AvgMag = MagnitudesIn[i];
				}
				else
				{
					AvgMag = (MagnitudesIn[i] + MagnitudesOut[i]) * 0.5;
				}

				if (AvgMag < SMALL_NUMBER)
				{
					AvgMag = SimplifiedPoints[i].TangentIn.Size();
				}

				const FVector SmoothedTangent = SmoothedDirections[i] * AvgMag;

				SimplifiedPoints[i].TangentIn = FMath::Lerp(SimplifiedPoints[i].TangentIn, SmoothedTangent, EffectiveSmoothing);
				SimplifiedPoints[i].TangentOut = FMath::Lerp(SimplifiedPoints[i].TangentOut, SmoothedTangent, EffectiveSmoothing);
			}
			else if (SmoothingMode == EPCGExTangentSmoothing::DirectionOnly)
			{
				double MagIn = MagnitudesIn[i];
				double MagOut = MagnitudesOut[i];

				if (MagIn < SMALL_NUMBER)
				{
					MagIn = SimplifiedPoints[i].TangentIn.Size();
				}
				if (MagOut < SMALL_NUMBER)
				{
					MagOut = SimplifiedPoints[i].TangentOut.Size();
				}

				const FVector SmoothedIn = SmoothedDirections[i] * MagIn;
				const FVector SmoothedOut = SmoothedDirections[i] * MagOut;

				SimplifiedPoints[i].TangentIn = FMath::Lerp(SimplifiedPoints[i].TangentIn, SmoothedIn, EffectiveSmoothing);
				SimplifiedPoints[i].TangentOut = FMath::Lerp(SimplifiedPoints[i].TangentOut, SmoothedOut, EffectiveSmoothing);
			}
		}

		if (!bIsClosed)
		{
			SimplifiedPoints[0].TangentIn = SimplifiedPoints[0].TangentOut;
			SimplifiedPoints[NumPoints - 1].TangentOut = SimplifiedPoints[NumPoints - 1].TangentIn;
		}
	}

	void FCurveSimplifier::FitSegmentMagnitudes(
		const FVector& P0Pos,
		const FVector& P1Pos,
		const FVector& Dir0,
		const FVector& Dir1,
		const TArray<FVector>& IntermediatePoints,
		const TArray<double>& TValues,
		double& OutMag0,
		double& OutMag1)
	{
		const FVector ChordDir = P1Pos - P0Pos;
		const double ChordLength = ChordDir.Size();
		const int32 N = IntermediatePoints.Num();

		if (N == 0 || TValues.Num() != N || ChordLength < SMALL_NUMBER)
		{
			OutMag0 = ChordLength;
			OutMag1 = ChordLength;
			return;
		}

		double AtA_00 = 0.0, AtA_01 = 0.0, AtA_11 = 0.0;
		double AtR_0 = 0.0, AtR_1 = 0.0;

		const double D0DotD1 = FVector::DotProduct(Dir0, Dir1);

		for (int32 i = 0; i < N; ++i)
		{
			const double t = TValues[i];
			const double h10 = H10(t);
			const double h11 = H11(t);
			const double h00 = H00(t);
			const double h01 = H01(t);

			const FVector Ri = IntermediatePoints[i] - h00 * P0Pos - h01 * P1Pos;

			AtA_00 += h10 * h10;
			AtA_01 += h10 * h11 * D0DotD1;
			AtA_11 += h11 * h11;

			AtR_0 += h10 * FVector::DotProduct(Dir0, Ri);
			AtR_1 += h11 * FVector::DotProduct(Dir1, Ri);
		}

		const double Det = AtA_00 * AtA_11 - AtA_01 * AtA_01;

		if (FMath::Abs(Det) < SMALL_NUMBER)
		{
			OutMag0 = ChordLength;
			OutMag1 = ChordLength;
			return;
		}

		const double InvDet = 1.0 / Det;

		OutMag0 = (AtA_11 * AtR_0 - AtA_01 * AtR_1) * InvDet;
		OutMag1 = (-AtA_01 * AtR_0 + AtA_00 * AtR_1) * InvDet;

		// Ensure positive magnitudes
		OutMag0 = FMath::Max(FMath::Abs(OutMag0), SMALL_NUMBER);
		OutMag1 = FMath::Max(FMath::Abs(OutMag1), SMALL_NUMBER);
	}

	void FCurveSimplifier::FitSegmentTangentsLS(
		const FVector& P0Pos,
		const FVector& P1Pos,
		const TArray<FVector>& IntermediatePoints,
		const TArray<double>& TValues,
		FVector& OutT0,
		FVector& OutT1)
	{
		const int32 N = IntermediatePoints.Num();
		const FVector ChordDir = P1Pos - P0Pos;
		const double ChordLength = ChordDir.Size();

		// No intermediate points - use chord-based tangents
		if (N == 0 || TValues.Num() != N)
		{
			FVector Dir = ChordDir.GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				Dir = FVector::ForwardVector;
			}
			OutT0 = Dir * ChordLength;
			OutT1 = Dir * ChordLength;
			return;
		}

		// Build normal equations for least-squares
		double AtA_00 = 0.0, AtA_01 = 0.0, AtA_11 = 0.0;
		FVector AtR_0 = FVector::ZeroVector;
		FVector AtR_1 = FVector::ZeroVector;

		for (int32 i = 0; i < N; ++i)
		{
			const double t = TValues[i];
			const double h10 = H10(t);
			const double h11 = H11(t);
			const double h00 = H00(t);
			const double h01 = H01(t);

			const FVector Ri = IntermediatePoints[i] - h00 * P0Pos - h01 * P1Pos;

			AtA_00 += h10 * h10;
			AtA_01 += h10 * h11;
			AtA_11 += h11 * h11;

			AtR_0 += h10 * Ri;
			AtR_1 += h11 * Ri;
		}

		const double Det = AtA_00 * AtA_11 - AtA_01 * AtA_01;

		if (FMath::Abs(Det) < SMALL_NUMBER)
		{
			FVector Dir = ChordDir.GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				Dir = FVector::ForwardVector;
			}
			OutT0 = Dir * ChordLength;
			OutT1 = Dir * ChordLength;
			return;
		}

		const double InvDet = 1.0 / Det;

		OutT0 = (AtA_11 * AtR_0 - AtA_01 * AtR_1) * InvDet;
		OutT1 = (-AtA_01 * AtR_0 + AtA_00 * AtR_1) * InvDet;

		// Only fix truly degenerate cases
		if (OutT0.ContainsNaN() || OutT0.Size() < SMALL_NUMBER)
		{
			OutT0 = ChordDir.GetSafeNormal() * ChordLength;
		}

		if (OutT1.ContainsNaN() || OutT1.Size() < SMALL_NUMBER)
		{
			OutT1 = ChordDir.GetSafeNormal() * ChordLength;
		}
	}

	void FCurveSimplifier::GetIntermediatePoints(
		const TConstPCGValueRange<FTransform>& OriginalPoints,
		int32 StartIdx,
		int32 EndIdx,
		int32 TotalPoints,
		bool bIsClosed,
		TArray<FVector>& OutPoints)
	{
		OutPoints.Empty();

		if (bIsClosed && EndIdx <= StartIdx)
		{
			// Wrap around case for closed loops
			for (int32 i = StartIdx + 1; i < TotalPoints; ++i)
			{
				OutPoints.Add(OriginalPoints[i].GetLocation());
			}
			for (int32 i = 0; i < EndIdx; ++i)
			{
				OutPoints.Add(OriginalPoints[i].GetLocation());
			}
		}
		else
		{
			for (int32 i = StartIdx + 1; i < EndIdx; ++i)
			{
				OutPoints.Add(OriginalPoints[i].GetLocation());
			}
		}
	}

	void FCurveSimplifier::ComputeChordLengthParams(
		const FVector& P0,
		const FVector& P1,
		const TArray<FVector>& IntermediatePoints,
		TArray<double>& OutTValues)
	{
		OutTValues.Empty();

		if (IntermediatePoints.Num() == 0)
		{
			return;
		}

		TArray<double> CumulativeLengths;
		CumulativeLengths.Reserve(IntermediatePoints.Num() + 2);

		double TotalLength = 0.0;
		FVector PrevPoint = P0;

		CumulativeLengths.Add(0.0);

		for (const FVector& Point : IntermediatePoints)
		{
			TotalLength += (Point - PrevPoint).Size();
			CumulativeLengths.Add(TotalLength);
			PrevPoint = Point;
		}

		TotalLength += (P1 - PrevPoint).Size();

		if (TotalLength > SMALL_NUMBER)
		{
			for (int32 i = 0; i < IntermediatePoints.Num(); ++i)
			{
				OutTValues.Add(CumulativeLengths[i + 1] / TotalLength);
			}
		}
		else
		{
			for (int32 i = 0; i < IntermediatePoints.Num(); ++i)
			{
				OutTValues.Add(static_cast<double>(i + 1) / static_cast<double>(IntermediatePoints.Num() + 1));
			}
		}
	}

	FVector FCurveSimplifier::EvaluateHermite(
		const FVector& P0, const FVector& T0,
		const FVector& P1, const FVector& T1,
		double t)
	{
		return H00(t) * P0 + H10(t) * T0 + H01(t) * P1 + H11(t) * T1;
	}
}
