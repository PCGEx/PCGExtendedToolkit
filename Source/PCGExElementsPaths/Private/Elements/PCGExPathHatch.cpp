// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathHatch.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "Async/ParallelFor.h"
#include "PCGParamData.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/PCGExMath.h"
#include "Math/PCGExMathDistances.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Sampling/PCGExSamplingUnionData.h"
#include "SubPoints/DataBlending/PCGExSubPointsBlendInterpolate.h"

#define LOCTEXT_NAMESPACE "PCGExPathHatchElement"
#define PCGEX_NAMESPACE PathHatch

namespace PCGExPathHatch
{
	// Tie at T=0.5 deterministically resolves to the smaller index so output ordering is stable
	// across runs regardless of edge orientation.
	FORCEINLINE int32 NearestOfTwo(const int32 A, const int32 B, const float T)
	{
		if (T < 0.5f) { return A; }
		if (T > 0.5f) { return B; }
		return FMath::Min(A, B);
	}

}

#if WITH_EDITOR
void UPCGExPathHatchSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!Blending)
		{
			Blending = NewObject<UPCGExSubPointsBlendInterpolate>(this, TEXT("Blending"));
		}
	}
	Super::PostInitProperties();
}
#endif

TArray<FPCGPinProperties> UPCGExPathHatchSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExBlending::Labels::SourceOverridesBlendingOps)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(PathHatch)
PCGEX_ELEMENT_BATCH_POINT_IMPL(PathHatch)

bool FPCGExPathHatchElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPathProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(PathHatch)

	if (Settings->bWriteAlpha)
	{
		PCGEX_VALIDATE_NAME(Settings->AlphaAttributeName)
	}

	PCGEX_BIND_INSTANCED_FACTORY(Blending, UPCGExSubPointsBlendInstancedFactory, PCGExBlending::Labels::SourceOverridesBlendingOps)

	Context->EndpointBlending = Settings->EndpointBlending;

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = Settings->GetMainOutputPin();

	return true;
}

bool FPCGExPathHatchElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathHatchElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathHatch)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs were not closed loops with at least 3 points and were skipped."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 3) { bHasInvalidInputs = true; return false; }
				if (!PCGExPaths::Helpers::GetClosedLoop(Entry)) { bHasInvalidInputs = true; return false; }
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("No valid closed-loop paths to hatch."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	(void)Context->OutputPaths->StageOutputs();
	PCGEX_OUTPUT_VALID_PATHS(MainPoints)

	return Context->TryComplete();
}

#pragma region PCGExPathHatch::FProcessor

namespace PCGExPathHatch
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathHatch::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		bUseSegmentCount = Settings->SegmentSpacingMode == EPCGExHatchSpacingMode::Count;
		bPerSegmentOutput = Settings->OutputMode == EPCGExHatchOutputMode::PerSegment;
		bWriteAlpha = Settings->bWriteAlpha;
		bRedistributeEvenly = Settings->bRedistributeEvenly;
		const bool bUseLineCount = Settings->LineSpacingMode == EPCGExHatchSpacingMode::Count;
		const bool bFilterSegments = Settings->bFilterSmallSegments;

		if (!Settings->AngleOffset.TryReadDataValue(PointDataFacade->Source, AngleOffsetDeg)) { return false; }
		if (!Settings->LineSpacing.TryReadDataValue(PointDataFacade->Source, LineSpacingValue)) { return false; }
		if (!Settings->LineOriginSlide.TryReadDataValue(PointDataFacade->Source, LineOriginSlideValue)) { return false; }
		if (!Settings->SegmentSpacing.TryReadDataValue(PointDataFacade->Source, SegmentSpacingValue)) { return false; }
		if (bFilterSegments && !Settings->MinSegmentLength.TryReadDataValue(PointDataFacade->Source, MinSegmentLengthValue)) { return false; }

		Projection = Settings->ProjectionDetails;
		if (!Projection.Init(PointDataFacade)) { return false; }

		const TConstPCGValueRange<FTransform> InTransforms = PointDataFacade->GetIn()->GetConstTransformValueRange();
		const int32 NumPts = InTransforms.Num();

		FBox2D ProjectedBounds;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_Project);
			PCGExPaths::BuildProjectedPoints2D(InTransforms, Projection, Projected, ProjectedBounds);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_BoxFit);
			if (Settings->BoxFitMode == EPCGExHatchBoxFitMode::BestFit || Settings->BoxFitMode == EPCGExHatchBoxFitMode::BestFitAccurate)
			{
				const PCGExMath::FBestFitPlane Plane2D(MakeArrayView(Projected.GetData(), Projected.Num()), Settings->BoxFitMode == EPCGExHatchBoxFitMode::BestFitAccurate);
				BoxAxisX = FVector2D(Plane2D.Axis[0].X, Plane2D.Axis[0].Y).GetSafeNormal();
				BoxAxisY = FVector2D(Plane2D.Axis[1].X, Plane2D.Axis[1].Y).GetSafeNormal();
				BoxCenter = FVector2D(Plane2D.Centroid.X, Plane2D.Centroid.Y);

				// Plane.Centroid is the data mean, not the OBB midpoint along oriented axes -- re-center
				// onto the oriented AABB so line origins span the actual extent.
				double MinX = MAX_dbl, MaxX = -MAX_dbl;
				double MinY = MAX_dbl, MaxY = -MAX_dbl;
				for (const FVector2D& P : Projected)
				{
					const FVector2D D = P - BoxCenter;
					const double DX = FVector2D::DotProduct(D, BoxAxisX);
					const double DY = FVector2D::DotProduct(D, BoxAxisY);
					MinX = FMath::Min(MinX, DX); MaxX = FMath::Max(MaxX, DX);
					MinY = FMath::Min(MinY, DY); MaxY = FMath::Max(MaxY, DY);
				}
				BoxCenter += BoxAxisX * ((MinX + MaxX) * 0.5) + BoxAxisY * ((MinY + MaxY) * 0.5);
				BoxHalfExtents = FVector2D((MaxX - MinX) * 0.5, (MaxY - MinY) * 0.5);
			}
			else
			{
				BoxAxisX = FVector2D(1, 0);
				BoxAxisY = FVector2D(0, 1);
				BoxCenter = ProjectedBounds.GetCenter();
				BoxHalfExtents = ProjectedBounds.GetExtent();
			}
		}

		const double AngleRad = FMath::DegreesToRadians(AngleOffsetDeg);
		const double CosA = FMath::Cos(AngleRad);
		const double SinA = FMath::Sin(AngleRad);
		LineDir2D = FVector2D(BoxAxisX.X * CosA - BoxAxisX.Y * SinA, BoxAxisX.X * SinA + BoxAxisX.Y * CosA).GetSafeNormal();
		LinePerp2D = FVector2D(-LineDir2D.Y, LineDir2D.X);

		// Closed-form perp-span: the OBB's extent along LinePerp2D is |HX·dot(X,P)| + |HY·dot(Y,P)|,
		// derived from projecting the four signed-corner offsets ±HX·X ±HY·Y onto LinePerp2D.
		const double DotXP = FVector2D::DotProduct(BoxAxisX, LinePerp2D);
		const double DotYP = FVector2D::DotProduct(BoxAxisY, LinePerp2D);
		const double PerpMax = FMath::Abs(BoxHalfExtents.X * DotXP) + FMath::Abs(BoxHalfExtents.Y * DotYP);
		const double PerpMin = -PerpMax;
		const double PerpRange = PerpMax - PerpMin;

		TArray<double> PerpOffsets;
		bool bPerpOffsetsWrapped = false;
		if (PerpRange > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			const bool bSlideApplied = !FMath::IsNearlyZero(LineOriginSlideValue);
			const bool bDoWrap = bSlideApplied && Settings->bWrapSlide;

			// Snap-to-divisor branch: Count mode is naturally seamless (Step = PerpRange / N), and
			// Distance mode with wrap snaps to the nearest divisor so the cyclic seam stays clean --
			// otherwise the (PerpRange mod Step) residual becomes a visible spacing artifact at the
			// wrap point. User's Step is treated as a density hint here, not an exact value.
			if (bUseLineCount || bDoWrap)
			{
				const int32 N = bUseLineCount
					? FMath::Max(1, FMath::FloorToInt(LineSpacingValue))
					: FMath::Max(1, FMath::RoundToInt(PerpRange / FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER, LineSpacingValue)));
				const double Step = PerpRange / static_cast<double>(N);
				PerpOffsets.SetNumUninitialized(N);
				const double StartOffset = (Settings->LineOrigin == EPCGExHatchLineOrigin::Center)
					? -PerpRange * 0.5 + Step * 0.5
					: PerpMin + Step * 0.5;
				for (int32 i = 0; i < N; ++i) { PerpOffsets[i] = StartOffset + i * Step; }
			}
			else
			{
				// Distance mode without wrap: honor the requested Step exactly. Ends may not tile cleanly.
				const double Step = FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER, LineSpacingValue);
				if (Settings->LineOrigin == EPCGExHatchLineOrigin::Center)
				{
					const int32 HalfN = FMath::FloorToInt((PerpRange * 0.5) / Step);
					const int32 Total = HalfN * 2 + 1;
					PerpOffsets.SetNumUninitialized(Total);
					for (int32 i = -HalfN; i <= HalfN; ++i) { PerpOffsets[i + HalfN] = i * Step; }
				}
				else
				{
					const int32 N = FMath::FloorToInt(PerpRange / Step) + 1;
					PerpOffsets.SetNumUninitialized(N);
					for (int32 i = 0; i < N; ++i) { PerpOffsets[i] = PerpMin + i * Step; }
				}
			}

			// Origin slide: Center treats the value as a fraction of the half-extent along LinePerp2D;
			// Start treats it as a fraction of the full extent. Signed in both modes.
			if (bSlideApplied)
			{
				const double SlideShift = (Settings->LineOrigin == EPCGExHatchLineOrigin::Center)
					? LineOriginSlideValue * PerpMax
					: LineOriginSlideValue * PerpRange;
				for (double& Offset : PerpOffsets) { Offset += SlideShift; }

				// Fold shifted-out lines back to the opposite side so the box stays tiled.
				if (Settings->bWrapSlide)
				{
					for (double& Offset : PerpOffsets) { Offset = PCGExMath::Tile(Offset, PerpMin, PerpMax); }
					// Wrap reshuffles the grid; re-sort so downstream lookups stay monotonic.
					PerpOffsets.Sort();
					bPerpOffsetsWrapped = true;
				}
			}
		}

		const int32 NumLines = PerpOffsets.Num();
		const int32 NumEdges = NumPts;
		Segments.Reset();
		// Tight upper bound for convex shapes (2 crossings/line); concave shapes will grow normally.
		Segments.Reserve(NumLines * 2);

		if (NumLines > 0 && NumEdges > 0)
		{
			// Project each edge onto LinePerp2D and LineDir2D once, then bucket-sweep crossings: each
			// hatch line at perpendicular offset O crosses an edge iff O lies in the edge's perp range
			// [pmin, pmax]. With PerpOffsets uniformly spaced (always true by construction), the index
			// range of crossing lines is O(1) to compute. Total cost: O(E + total crossings) instead of
			// O(L × E) brute force.
			struct FEdgeProj
			{
				double P0, P1, D0, D1;
				int32 I0, I1;
			};

			const double PerpOffset0 = PerpOffsets[0];
			const double PerpStep = (NumLines > 1) ? (PerpOffsets[1] - PerpOffsets[0]) : 1.0;
			const double InvPerpStep = 1.0 / PerpStep;

			// Half-open [Lo, Hi) so a vertex shared by two edges is claimed exactly once (by the edge
			// where it's the lower-perp endpoint). Closed would double-count when a hatch offset falls
			// exactly on an interior subdivision vertex, producing two zero-length pairs and dropping
			// the real segment. Closed fallback at Hi == PerpMax keeps boundary lines on the top edge;
			// PerpMin needs no fallback since [Lo, Hi) always includes Lo.
			//
			// Wrap-aware: fmod can introduce sub-epsilon drift, so binary-search the wrapped grid instead
			// of trusting the closed-form index at the seam.
			auto FindLineRange = [&](const double Lo, const double Hi, int32& OutFirst, int32& OutLast)
			{
				const bool bHiAtBoundary = FMath::IsNearlyEqual(Hi, PerpMax);
				if (bPerpOffsetsWrapped)
				{
					OutFirst = Algo::LowerBound(PerpOffsets, Lo);
					OutLast = bHiAtBoundary
						? Algo::UpperBound(PerpOffsets, Hi) - 1
						: Algo::LowerBound(PerpOffsets, Hi) - 1;
				}
				else
				{
					OutFirst = FMath::Max(0, FMath::CeilToInt((Lo - PerpOffset0) * InvPerpStep));
					const double UpperRatio = (Hi - PerpOffset0) * InvPerpStep;
					OutLast = FMath::Min(NumLines - 1, bHiAtBoundary
						? FMath::FloorToInt(UpperRatio)
						: FMath::CeilToInt(UpperRatio) - 1);
				}
			};

			TArray<FEdgeProj> EdgeProjs;
			TArray<int32> LineCounts;
			TArray<int32> LineStart;
			TArray<FCrossing> Crossings;
			int32 TotalCrossings = 0;

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_EdgeProjs);
				EdgeProjs.SetNumUninitialized(NumEdges);
				for (int32 e = 0; e < NumEdges; ++e)
				{
					const int32 I0 = e;
					const int32 I1 = (e + 1) % NumPts;
					const FVector2D D0v = Projected[I0] - BoxCenter;
					const FVector2D D1v = Projected[I1] - BoxCenter;
					FEdgeProj& EP = EdgeProjs[e];
					EP.P0 = FVector2D::DotProduct(D0v, LinePerp2D);
					EP.P1 = FVector2D::DotProduct(D1v, LinePerp2D);
					EP.D0 = FVector2D::DotProduct(D0v, LineDir2D);
					EP.D1 = FVector2D::DotProduct(D1v, LineDir2D);
					EP.I0 = I0;
					EP.I1 = I1;
				}
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_Count);
				LineCounts.SetNumZeroed(NumLines);
				for (const FEdgeProj& EP : EdgeProjs)
				{
					const double Lo = FMath::Min(EP.P0, EP.P1);
					const double Hi = FMath::Max(EP.P0, EP.P1);
					if (Hi - Lo < UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }
					int32 First, Last;
					FindLineRange(Lo, Hi, First, Last);
					for (int32 L = First; L <= Last; ++L) { ++LineCounts[L]; }
				}
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_Fill);
				LineStart.SetNumUninitialized(NumLines + 1);
				LineStart[0] = 0;
				for (int32 L = 0; L < NumLines; ++L) { LineStart[L + 1] = LineStart[L] + LineCounts[L]; }
				TotalCrossings = LineStart[NumLines];

				// Reuse LineCounts as a per-line fill cursor for the bucket scatter below.
				FMemory::Memzero(LineCounts.GetData(), LineCounts.Num() * sizeof(int32));

				Crossings.SetNumUninitialized(TotalCrossings);

				for (const FEdgeProj& EP : EdgeProjs)
				{
					const double Lo = FMath::Min(EP.P0, EP.P1);
					const double Hi = FMath::Max(EP.P0, EP.P1);
					if (Hi - Lo < UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }
					int32 First, Last;
					FindLineRange(Lo, Hi, First, Last);
					if (Last < First) { continue; }

					const double InvDeltaP = 1.0 / (EP.P1 - EP.P0);
					for (int32 L = First; L <= Last; ++L)
					{
						const double U = (PerpOffsets[L] - EP.P0) * InvDeltaP;
						const int32 Slot = LineStart[L] + LineCounts[L]++;
						FCrossing& C = Crossings[Slot];
						C.EdgeI0 = EP.I0;
						C.EdgeI1 = EP.I1;
						C.TAlongLine = FMath::Lerp(EP.D0, EP.D1, U);
						C.EdgeT = static_cast<float>(U);
					}
				}
			}

			TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_PairSegments);
			for (int32 L = 0; L < NumLines; ++L)
			{
				const int32 Begin = LineStart[L];
				int32 NumCrossings = LineStart[L + 1] - Begin;
				if (NumCrossings < 2) { continue; }

				FCrossing* CData = Crossings.GetData() + Begin;

				// Convex shapes hit each line exactly twice -- fast-path the common case to skip the
				// generic sort's lambda and recursion overhead.
				if (NumCrossings == 2)
				{
					if (CData[1].TAlongLine < CData[0].TAlongLine) { Swap(CData[0], CData[1]); }
				}
				else
				{
					Algo::Sort(MakeArrayView(CData, NumCrossings), [](const FCrossing& A, const FCrossing& B) { return A.TAlongLine < B.TAlongLine; });
				}

				// Tangency parity: a line exactly through a perp-local-extremum vertex is claimed by BOTH
				// adjacent edges (the half-open rule only disambiguates monotonic pass-through vertices),
				// yielding two coincident crossings. A tangent touch spans nothing inside -- drop both, or
				// the duplicate shifts pairing and truncates/splits every segment after it on this line.
				{
					int32 Kept = 0;
					int32 CIdx = 0;
					while (CIdx < NumCrossings)
					{
						if (CIdx + 1 < NumCrossings)
						{
							const FCrossing& A = CData[CIdx];
							const FCrossing& B = CData[CIdx + 1];
							const bool bSharedVtx = A.EdgeI0 == B.EdgeI0 || A.EdgeI0 == B.EdgeI1 || A.EdgeI1 == B.EdgeI0 || A.EdgeI1 == B.EdgeI1;
							if (bSharedVtx && FMath::IsNearlyEqual(A.TAlongLine, B.TAlongLine, UE_DOUBLE_KINDA_SMALL_NUMBER))
							{
								CIdx += 2;
								continue;
							}
						}
						if (Kept != CIdx) { CData[Kept] = CData[CIdx]; }
						Kept++;
						CIdx++;
					}
					NumCrossings = Kept;
					if (NumCrossings < 2) { continue; }
				}

				const int32 PairCount = NumCrossings / 2;
				for (int32 P = 0; P < PairCount; ++P)
				{
					const FCrossing& Entry = CData[P * 2];
					const FCrossing& Exit = CData[P * 2 + 1];

					FHatchSegment Seg;
					Seg.StartEdgeI0 = Entry.EdgeI0; Seg.StartEdgeI1 = Entry.EdgeI1; Seg.StartEdgeT = Entry.EdgeT;
					Seg.EndEdgeI0 = Exit.EdgeI0; Seg.EndEdgeI1 = Exit.EdgeI1; Seg.EndEdgeT = Exit.EdgeT;

					// World endpoints lerp the original world edges, not Projection.Unproject(2D crossing) --
					// the projection is rotation-only, so unprojecting (x,y,0) collapses depth and snaps
					// off-origin BestFit/LocalTangent inputs onto the world-origin plane.
					Seg.WorldStart = FMath::Lerp(InTransforms[Entry.EdgeI0].GetLocation(), InTransforms[Entry.EdgeI1].GetLocation(), static_cast<double>(Entry.EdgeT));
					Seg.WorldEnd = FMath::Lerp(InTransforms[Exit.EdgeI0].GetLocation(), InTransforms[Exit.EdgeI1].GetLocation(), static_cast<double>(Exit.EdgeT));
					Seg.Length = FVector::Distance(Seg.WorldStart, Seg.WorldEnd);

					// Collocated crossing pair (tangent or sliver geometry): a zero-length segment emits
					// two coincident points that downstream path nodes can't frame. Drop unconditionally --
					// bFilterSmallSegments is opt-in and defaults off.
					if (Seg.Length < UE_DOUBLE_KINDA_SMALL_NUMBER) { continue; }

					Seg.SourceStart = NearestOfTwo(Entry.EdgeI0, Entry.EdgeI1, Entry.EdgeT);
					Seg.SourceEnd = NearestOfTwo(Exit.EdgeI0, Exit.EdgeI1, Exit.EdgeT);

					if (bFilterSegments && Seg.Length < MinSegmentLengthValue) { continue; }

					int32 NumInterior = 0;
					if (bUseSegmentCount)
					{
						NumInterior = FMath::Max(0, FMath::FloorToInt(SegmentSpacingValue));
					}
					else
					{
						const double Step = FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER, SegmentSpacingValue);
						NumInterior = FMath::Max(0, FMath::FloorToInt(Seg.Length / Step));

						// When Length is an exact multiple of Step, the last interior point lands on WorldEnd,
						// emitting collocated points at the segment end (which downstream nodes choke on --
						// e.g. Path : Extrude can't derive a terminal tangent). Keep interiors strictly inside.
						if (NumInterior > 0 && NumInterior * Step >= Seg.Length - UE_DOUBLE_KINDA_SMALL_NUMBER)
						{
							NumInterior--;
						}
					}
					Seg.NumPoints = NumInterior + 2;

					Segments.Add(MoveTemp(Seg));
				}
			}
		}

		if (Segments.IsEmpty())
		{
			PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Hatch produced no kept segments for an input."));
		}

		CompleteWork();
		
		return bIsProcessorValid;
	}

	void FProcessor::CompleteWork()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PathHatch_Complete);
		const int32 NumSegments = Segments.Num();
		if (NumSegments == 0) { return; }

		const bool bDoEndpointBlending = Settings->bDoEndpointBlending;

		int32 RunningTotal = 0;
		for (FHatchSegment& Seg : Segments)
		{
			Seg.OutStart = RunningTotal;
			RunningTotal += Seg.NumPoints;
		}
		const int32 MergedTotal = RunningTotal;

		// Reserve our writable attributes from blender modification.
		if (bWriteAlpha) { ProtectedAttributes.Add(Settings->AlphaAttributeName); }

		const int32 InputIO = PointDataFacade->Source->IOIndex;

		TArray<PCGExData::FWeightedPoint> WeightedPoints;
		TArray<PCGEx::FOpStats> Trackers;
		TSharedPtr<PCGExSampling::FSampingUnionData> TempUnion;
		if (bDoEndpointBlending)
		{
			TempUnion = MakeShared<PCGExSampling::FSampingUnionData>();
			TempUnion->WeightRange = -2; // pass weights verbatim -- exact edge-T lerp
		}

		auto FillIdxMapping = [](TArray<int32>& IdxMapping, const int32 Base, const FHatchSegment& Seg)
		{
			IdxMapping[Base] = Seg.SourceStart;
			IdxMapping[Base + Seg.NumPoints - 1] = Seg.SourceEnd;
			const int32 NumInterior = Seg.NumPoints - 2;
			for (int32 i = 0; i < NumInterior; ++i)
			{
				const float Alpha = static_cast<float>(i + 1) / static_cast<float>(NumInterior + 1);
				IdxMapping[Base + 1 + i] = NearestOfTwo(Seg.SourceStart, Seg.SourceEnd, Alpha);
			}
		};

		// Endpoint blending -- scratch (TempUnion / WeightedPoints / Trackers) passed in so callers
		// can supply per-thread arrays when invoked from a parallel context.
		auto BlendSegmentEndpoints = [&](
			const TSharedPtr<PCGExBlending::FUnionBlender>& Blender,
			const FHatchSegment& Seg,
			const int32 OutA,
			const int32 OutB,
			const TSharedPtr<PCGExSampling::FSampingUnionData>& InTempUnion,
			TArray<PCGExData::FWeightedPoint>& InWeightedPoints,
			TArray<PCGEx::FOpStats>& InTrackers)
		{
			InTempUnion->Reset();
			InTempUnion->AddWeighted_Unsafe(PCGExData::FElement(Seg.StartEdgeI0, InputIO), 1.0 - static_cast<double>(Seg.StartEdgeT));
			InTempUnion->AddWeighted_Unsafe(PCGExData::FElement(Seg.StartEdgeI1, InputIO), static_cast<double>(Seg.StartEdgeT));
			Blender->MergeSingle(OutA, InTempUnion, InWeightedPoints, InTrackers);

			InTempUnion->Reset();
			InTempUnion->AddWeighted_Unsafe(PCGExData::FElement(Seg.EndEdgeI0, InputIO), 1.0 - static_cast<double>(Seg.EndEdgeT));
			InTempUnion->AddWeighted_Unsafe(PCGExData::FElement(Seg.EndEdgeI1, InputIO), static_cast<double>(Seg.EndEdgeT));
			Blender->MergeSingle(OutB, InTempUnion, InWeightedPoints, InTrackers);
		};

		auto BuildEndpointBlender = [&](
			const TSharedPtr<PCGExData::FFacade>& TargetFacade,
			TArray<PCGEx::FOpStats>& InTrackers) -> TSharedPtr<PCGExBlending::FUnionBlender>
		{
			TSharedPtr<PCGExBlending::FUnionBlender> Blender = MakeShared<PCGExBlending::FUnionBlender>(
				&Context->EndpointBlending,
				&Settings->EndpointCarryOver,
				PCGExMath::GetDistances());

			TArray<TSharedRef<PCGExData::FFacade>> UnionSources;
			UnionSources.Add(PointDataFacade);
			Blender->AddSources(UnionSources, &ProtectedAttributes);

			if (!Blender->Init(Context, TargetFacade, PCGExData::EProxyFlags::Direct)) { return nullptr; }
			Blender->InitTrackers(InTrackers);
			return Blender;
		};

		auto BuildSubBlender = [&](const TSharedPtr<PCGExData::FFacade>& TargetFacade) -> TSharedPtr<FPCGExSubPointsBlendOperation>
		{
			TSharedPtr<FPCGExSubPointsBlendOperation> Op = Context->Blending->CreateOperation();
			Op->bClosedLoop = false;
			if (!Op->PrepareForData(Context, TargetFacade, &ProtectedAttributes)) { return nullptr; }
			return Op;
		};

		if (!bPerSegmentOutput)
		{
			MergedIO = Context->OutputPaths->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::New);
			if (!MergedIO) { bIsProcessorValid = false; return; }

			// EIOInit::New inherits the source's @Data attributes, including ClosedLoop=true from the
			// input loop -- the merged polyline is open, same as the per-segment outputs.
			PCGExPaths::Helpers::SetClosedLoop(MergedIO, false);

			UPCGBasePointData* OutPoints = MergedIO->GetOut();
			const UPCGBasePointData* InPoints = PointDataFacade->GetIn();
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPoints, MergedTotal, InPoints->GetAllocatedProperties());

			TArray<int32>& IdxMapping = MergedIO->GetIdxMapping(MergedTotal);
			for (const FHatchSegment& Seg : Segments) { FillIdxMapping(IdxMapping, Seg.OutStart, Seg); }
			MergedIO->ConsumeIdxMapping(EPCGPointNativeProperties::All);

			PCGEX_MAKE_SHARED(MergedFacade, PCGExData::FFacade, MergedIO.ToSharedRef())
			MergedOutputFacade = MergedFacade;

			// Pre-create the alpha writer so the parallel pass's SetValue calls don't race on buffer creation.
			if (bWriteAlpha)
			{
				AlphaWriter = MergedOutputFacade->GetWritable<double>(Settings->AlphaAttributeName, 0.0, true, PCGExData::EBufferInit::New);
			}

			SubBlending = BuildSubBlender(MergedOutputFacade);
			if (!SubBlending) { bIsProcessorValid = false; return; }

			if (bDoEndpointBlending)
			{
				EndpointBlender = BuildEndpointBlender(MergedOutputFacade, Trackers);
				if (!EndpointBlender) { bIsProcessorValid = false; return; }

				for (const FHatchSegment& Seg : Segments)
				{
					BlendSegmentEndpoints(EndpointBlender, Seg, Seg.OutStart, Seg.OutStart + Seg.NumPoints - 1, TempUnion, WeightedPoints, Trackers);
				}
			}
		}
		else
		{
			SegmentIOs.SetNum(NumSegments);
			SegmentFacades.SetNum(NumSegments);
			SubBlendings.SetNum(NumSegments);
			if (bWriteAlpha) { SegmentAlphaWriters.SetNum(NumSegments); }
			if (bDoEndpointBlending) { EndpointBlenders.SetNum(NumSegments); }

			// EmplaceBatch returns false only on InitializeOutput rejection (cancellation): abort fully.
			if (!Context->OutputPaths->EmplaceBatch(SegmentIOs, PointDataFacade->Source, PCGExData::EIOInit::New))
			{
				bIsProcessorValid = false;
				return;
			}

			// Initialize one segment in isolation. Takes per-call scratch so it's safe to invoke
			// from either the serial pre-warm or the parallel sub-loop below.
			auto InitSegment = [&](
				const int32 SegIdx,
				const TSharedPtr<PCGExSampling::FSampingUnionData>& InTempUnion,
				TArray<PCGExData::FWeightedPoint>& InWeightedPoints,
				TArray<PCGEx::FOpStats>& InTrackers) -> bool
			{
				const FHatchSegment& Seg = Segments[SegIdx];
				const TSharedPtr<PCGExData::FPointIO>& SegIO = SegmentIOs[SegIdx];

				PCGExPaths::Helpers::SetClosedLoop(SegIO, false);

				UPCGBasePointData* OutPoints = SegIO->GetOut();
				const UPCGBasePointData* InPoints = PointDataFacade->GetIn();
				PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutPoints, Seg.NumPoints, InPoints->GetAllocatedProperties());

				TArray<int32>& IdxMapping = SegIO->GetIdxMapping(Seg.NumPoints);
				FillIdxMapping(IdxMapping, 0, Seg);
				SegIO->ConsumeIdxMapping(EPCGPointNativeProperties::All);

				PCGEX_MAKE_SHARED(SegFacade, PCGExData::FFacade, SegIO.ToSharedRef())
				SegmentFacades[SegIdx] = SegFacade;

				if (bWriteAlpha)
				{
					SegmentAlphaWriters[SegIdx] = SegFacade->GetWritable<double>(Settings->AlphaAttributeName, 0.0, true, PCGExData::EBufferInit::New);
				}

				SubBlendings[SegIdx] = BuildSubBlender(SegFacade);
				if (!SubBlendings[SegIdx]) { return false; }

				if (bDoEndpointBlending)
				{
					EndpointBlenders[SegIdx] = BuildEndpointBlender(SegFacade, InTrackers);
					if (!EndpointBlenders[SegIdx]) { return false; }
					BlendSegmentEndpoints(EndpointBlenders[SegIdx], Seg, 0, Seg.NumPoints - 1, InTempUnion, InWeightedPoints, InTrackers);
				}

				return true;
			};

			// Pre-warm segment 0 serially. FUnionBlender::AddSources/Init and PrepareForData may
			// register attribute readers on the shared source facade via Capture; doing the first
			// iteration single-threaded ensures any such state mutation lands before parallel
			// iterations could race on it.
			if (!InitSegment(0, TempUnion, WeightedPoints, Trackers))
			{
				bIsProcessorValid = false;
				return;
			}

			// Parallel init for segments 1..N-1. Each iteration owns its scratch arrays so
			// FUnionBlender::MergeSingle / InitTrackers don't contend on shared state.
			std::atomic<bool> bParallelValid{true};
			ParallelFor(NumSegments - 1, [&](int32 i)
			{
				if (!bParallelValid.load(std::memory_order_relaxed)) { return; }

				TArray<PCGExData::FWeightedPoint> LocalWeightedPoints;
				TArray<PCGEx::FOpStats> LocalTrackers;
				TSharedPtr<PCGExSampling::FSampingUnionData> LocalTempUnion;
				if (bDoEndpointBlending)
				{
					LocalTempUnion = MakeShared<PCGExSampling::FSampingUnionData>();
					LocalTempUnion->WeightRange = -2;
				}

				if (!InitSegment(i + 1, LocalTempUnion, LocalWeightedPoints, LocalTrackers))
				{
					bParallelValid.store(false, std::memory_order_relaxed);
				}
			});

			if (!bParallelValid.load(std::memory_order_relaxed))
			{
				bIsProcessorValid = false;
				return;
			}
		}

		StartParallelLoopForRange(NumSegments, 16);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			const FHatchSegment& Seg = Segments[Index];

			// Raw pointers (rather than TSharedPtr) to avoid atomic refcount touches each iteration.
			// The shared pointers held by the processor outlive the parallel scope.
			PCGExData::FFacade* OutFacade = bPerSegmentOutput ? SegmentFacades[Index].Get() : MergedOutputFacade.Get();
			FPCGExSubPointsBlendOperation* SegSubBlending = bPerSegmentOutput ? SubBlendings[Index].Get() : SubBlending.Get();
			const int32 OutBase = bPerSegmentOutput ? 0 : Seg.OutStart;

			TPCGValueRange<FTransform> OutTransforms = OutFacade->GetOut()->GetTransformValueRange(false);

			const int32 NumInterior = Seg.NumPoints - 2;
			const int32 OutA = OutBase;
			const int32 OutB = OutBase + Seg.NumPoints - 1;

			OutTransforms[OutA].SetLocation(Seg.WorldStart);
			OutTransforms[OutB].SetLocation(Seg.WorldEnd);

			double StepSize = 0.0;
			double StartOffset = 0.0;
			if (NumInterior > 0)
			{
				if (bUseSegmentCount || bRedistributeEvenly)
				{
					StepSize = Seg.Length / static_cast<double>(NumInterior + 1);
					StartOffset = StepSize;
				}
				else
				{
					StepSize = FMath::Max(UE_DOUBLE_KINDA_SMALL_NUMBER, SegmentSpacingValue);
					StartOffset = StepSize;
				}
			}

			const FVector Dir = Seg.Length > UE_DOUBLE_KINDA_SMALL_NUMBER
				? (Seg.WorldEnd - Seg.WorldStart) / Seg.Length
				: FVector::ZeroVector;

			PCGExPaths::FPathMetrics Metrics = PCGExPaths::FPathMetrics(Seg.WorldStart);
			for (int32 i = 0; i < NumInterior; ++i)
			{
				const FVector Position = Seg.WorldStart + Dir * (StartOffset + i * StepSize);
				OutTransforms[OutBase + 1 + i].SetLocation(Position);
				Metrics.Add(Position);
			}
			Metrics.Add(Seg.WorldEnd);

			if (bWriteAlpha)
			{
				// Alpha writers were pre-created in CompleteWork -- no buffer-creation race here.
				PCGExData::TBuffer<double>* W = (bPerSegmentOutput ? SegmentAlphaWriters[Index] : AlphaWriter).Get();
				if (W)
				{
					W->SetValue(OutA, 0.0);
					W->SetValue(OutB, 1.0);
					for (int32 i = 0; i < NumInterior; ++i)
					{
						const float Alpha = static_cast<float>(i + 1) / static_cast<float>(NumInterior + 1);
						W->SetValue(OutBase + 1 + i, static_cast<double>(Alpha));
					}
				}
			}

			// Endpoints A and B already hold the correct attrs from CompleteWork (snap-to-nearest by
			// default, edge-T lerp if endpoint blending is enabled); SubBlending only fills interior.
			if (SegSubBlending && NumInterior > 0)
			{
				PCGExData::FScope SubScope = OutFacade->Source->GetOutScope(OutBase + 1, NumInterior);
				SegSubBlending->ProcessSubPoints(
					OutFacade->Source->GetOutPoint(OutA),
					OutFacade->Source->GetOutPoint(OutB),
					SubScope,
					Metrics);
			}
			
			if (bPerSegmentOutput)
			{
				OutFacade->WriteFastest(TaskManager);
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		if (!bPerSegmentOutput)
		{
			if (MergedOutputFacade) { MergedOutputFacade->WriteFastest(TaskManager); }
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
