// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchPlacement.h"

#include "Lattice/PCGExLatticeBasis.h"
#include "Sketch/PCGExClusterSketchAuthoringSettings.h"
#include "Sketch/PCGExClusterSketchModel.h"

namespace PCGExSketchPlacement
{
	// Screen-constant pick radius: world radius grows with distance so elements keep a steady picking
	// footprint. The floor keeps close-up picking from collapsing to a point.
	constexpr double PickTan = 0.0125;
	constexpr double MinPickRadius = 4.0;

	// Hysteresis. Distances are normalized to screen radii, so these compare fairly at any depth: an
	// incumbent guide holds out to a wider radius than it took to capture, and a rival must beat it by
	// a margin -- together, the two rules are what stop a guide flickering between neighbours.
	constexpr double ReleaseScale = 1.6;
	constexpr double SwitchMargin = 0.35;

	// Every guide runs through the anchor, so within this many screen radii of it they are
	// indistinguishable and none may capture.
	constexpr double MinTravelRadii = 3.0;

	// How far out a candidate preview starts brightening, as a multiple of the capture radius. Wider
	// than the capture itself so a rail lights up while you are still swinging toward it.
	constexpr double PreviewFalloffScale = 4.0;

	// Below this, a direction pair counts as the same line and two lines count as parallel.
	constexpr double ParallelEpsilon = 1.0e-4;

	// |cos| between ray and work-plane normal below which the plane is EDGE-ON. Far coarser than
	// ParallelEpsilon on purpose: long before true parallelism the intersection is tens of anchor
	// distances away, so the plane has stopped being usable well above any numerical limit.
	constexpr double MinPlaneCosine = 0.05;

	double ScreenRadiusAt(const FRay& LocalRay, const FVector& LocalPos, const double InMinWorldRadius)
	{
		const double Dist = FVector::Dist(LocalRay.Origin, LocalPos);
		return FMath::Max3(MinPickRadius, InMinWorldRadius, Dist * PickTan);
	}
}

bool FPCGExSketchPlacementSolver::ClosestPointOnGuide(const FRay& LocalRay, const FPCGExSketchGuide& Guide, FVector& OutPoint, double& OutDistance)
{
	// Closest approach of two infinite lines. Both directions are unit, so the Gram terms collapse to
	// 1 and the determinant is 1 - (D1.D2)^2 -- zero exactly when they are parallel.
	const double B = FVector::DotProduct(LocalRay.Direction, Guide.Direction);
	const double Denominator = 1.0 - B * B;
	if (FMath::Abs(Denominator) <= PCGExSketchPlacement::ParallelEpsilon)
	{
		return false;
	}

	const FVector W = LocalRay.Origin - Guide.Origin;
	const double D = FVector::DotProduct(LocalRay.Direction, W);
	const double E = FVector::DotProduct(Guide.Direction, W);

	const double RayT = (B * E - D) / Denominator;
	if (RayT < 0.0)
	{
		// Behind the viewer.
		return false;
	}

	OutPoint = Guide.Origin + Guide.Direction * ((E - B * D) / Denominator);
	OutDistance = FVector::Dist(LocalRay.Origin + LocalRay.Direction * RayT, OutPoint);
	return true;
}

bool FPCGExSketchPlacementSolver::IntersectWorkPlane(const FRay& LocalRay, FVector& OutPoint) const
{
	// Viewed edge-on the work plane cannot answer at all -- an orthographic Front view of a Z-up plane
	// is exactly parallel to it -- so the view plane through the anchor stands in. It is never parallel
	// to the ray by construction, and it is what a drag rode before work planes existed.
	FVector Normal = PlaneNormal;
	if (FMath::Abs(FVector::DotProduct(LocalRay.Direction, PlaneNormal)) < PCGExSketchPlacement::MinPlaneCosine)
	{
		Normal = -LocalRay.Direction;
	}

	const double Denominator = FVector::DotProduct(LocalRay.Direction, Normal);
	if (FMath::Abs(Denominator) <= PCGExSketchPlacement::ParallelEpsilon)
	{
		return false;
	}

	const double T = FVector::DotProduct(Anchor - LocalRay.Origin, Normal) / Denominator;
	if (T <= 0.0)
	{
		return false;
	}

	OutPoint = LocalRay.Origin + LocalRay.Direction * T;
	return true;
}

void FPCGExSketchPlacementSolver::BeginGesture(const FVector& InAnchor, const FPCGExLatticeBasis* Basis)
{
	Anchor = InAnchor;
	ResetGuide();
	Candidates.Reset();
	CandidateScores.Reset();

	// A 2-axis lattice has exactly one plane worth working on; anything else has no natural one and
	// falls back to Z-up, matching what free-form authoring expects of a ground plane.
	PlaneNormal = FVector::UpVector;
	if (Basis && Basis->NumAxes == 2)
	{
		FVector Complement[3];
		if (Basis->GetComplementBasis(Complement) > 0)
		{
			PlaneNormal = Complement[0];
		}
	}
}

void FPCGExSketchPlacementSolver::AddCandidate(const EPCGExSketchGuideSource InSource, const int32 InSourceIndex, const FVector& InDirection)
{
	const FVector Dir = InDirection.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}

	for (const FPCGExSketchGuide& Existing : Candidates)
	{
		if (FMath::Abs(FVector::DotProduct(Existing.Direction, Dir)) >= 1.0 - PCGExSketchPlacement::ParallelEpsilon)
		{
			return;
		}
	}

	FPCGExSketchGuide& Guide = Candidates.AddDefaulted_GetRef();
	Guide.Source = InSource;
	Guide.SourceIndex = InSourceIndex;
	Guide.Origin = Anchor;
	Guide.Direction = Dir;
}

void FPCGExSketchPlacementSolver::BuildCandidates(const FPCGExClusterSketchModel* Model, const int32 AnchorVertex, const FPCGExLatticeBasis* Basis, const bool bPointWillSnap)
{
	// Scores are parallel to Candidates and are refilled by the next Resolve; clearing them here keeps
	// the two in step even for the window in between.
	Candidates.Reset();
	CandidateScores.Reset();

	const UPCGExClusterSketchAuthoringSettings* Options = UPCGExClusterSketchAuthoringSettings::Get();

	// Build order IS cycle order, so it runs most-useful-first.
	if (Basis && Basis->bValid)
	{
		if (Options->bGuideLatticeAxes)
		{
			for (int32 k = 0; k < Basis->NumAxes; ++k)
			{
				AddCandidate(EPCGExSketchGuideSource::LatticeAxis, k, Basis->AxisVecs[k]);
			}
		}

		// Snapping DISCARDS the complement, so offering it on a point that will snap would be a guide
		// that provably does nothing.
		if (Options->bGuideComplementAxis && !bPointWillSnap)
		{
			FVector Complement[3];
			const int32 NumComplement = Basis->GetComplementBasis(Complement);
			for (int32 k = 0; k < NumComplement; ++k)
			{
				AddCandidate(EPCGExSketchGuideSource::Complement, k, Complement[k]);
			}
		}
	}

	if (Options->bGuideIncidentEdges && Model && Model->Vertices.IsValidIndex(AnchorVertex))
	{
		for (int32 e = 0; e < Model->Edges.Num(); ++e)
		{
			const FPCGExClusterSketchEdge& Edge = Model->Edges[e];
			const int32 Other = Edge.A == AnchorVertex ? Edge.B : (Edge.B == AnchorVertex ? Edge.A : INDEX_NONE);
			if (Other == INDEX_NONE || !Model->Vertices.IsValidIndex(Other))
			{
				continue;
			}
			AddCandidate(EPCGExSketchGuideSource::IncidentEdge, e, FPCGExClusterSketchModel::ResolvedLocation(Model->Vertices[Other], Basis) - Anchor);
		}
	}

	if (Basis && Basis->bValid && Options->bGuideLatticeWalks)
	{
		// The walk set contains the axis unit offsets by construction; skipping them keeps this toggle
		// disjoint from the axes one instead of silently re-enabling it.
		for (int32 w = 0; w < Basis->WalkOffsets.Num(); ++w)
		{
			const FIntVector& Offset = Basis->WalkOffsets[w];
			bool bIsAxis = false;
			for (int32 k = 0; k < Basis->NumAxes && !bIsAxis; ++k)
			{
				// FIntVector has no unary negation, so the antipodal test sums to zero instead.
				const FIntVector Unit = Basis->AxisUnitOffset(k);
				bIsAxis = Offset == Unit || Offset + Unit == FIntVector::ZeroValue;
			}
			if (bIsAxis)
			{
				continue;
			}
			AddCandidate(EPCGExSketchGuideSource::LatticeWalk, w, Basis->CoordToWorld(Offset) - Basis->Origin);
		}
	}

	// Identity, not slot: a rebuild between frames must not drop what was captured, and a candidate the
	// options just switched off must not stay latched.
	if (ActiveGuide.IsValid())
	{
		const int32 Found = Candidates.IndexOfByKey(ActiveGuide);
		if (Found == INDEX_NONE)
		{
			ResetGuide();
		}
		else
		{
			ActiveGuide = Candidates[Found];
			if (Mode == EMode::Forced)
			{
				ForcedIndex = Found;
			}
		}
	}
}

void FPCGExSketchPlacementSolver::UpdateActiveGuide(const FRay& LocalRay, const FVector& BasePoint, const bool bHasBasePoint)
{
	const int32 IncumbentIndex = ActiveGuide.IsValid() ? Candidates.IndexOfByKey(ActiveGuide) : INDEX_NONE;
	double IncumbentScore = TNumericLimits<double>::Max();
	int32 BestIndex = INDEX_NONE;
	double BestScore = TNumericLimits<double>::Max();

	// Scored in EVERY mode, not just while inferring: the candidate preview reads these to brighten a
	// rail as the cursor swings toward it, which it must keep doing while a guide is forced or held.
	CandidateScores.SetNum(Candidates.Num(), EAllowShrinking::Yes);
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		CandidateScores[i] = TNumericLimits<double>::Max();

		FVector OnGuide = FVector::ZeroVector;
		double Distance = 0.0;
		if (!ClosestPointOnGuide(LocalRay, Candidates[i], OnGuide, Distance))
		{
			continue;
		}

		// Normalized to screen radii so candidates at different depths compare fairly.
		const double Score = Distance / PCGExSketchPlacement::ScreenRadiusAt(LocalRay, OnGuide);
		CandidateScores[i] = Score;

		if (i == IncumbentIndex)
		{
			IncumbentScore = Score;
		}
		else if (Score < BestScore)
		{
			BestScore = Score;
			BestIndex = i;
		}
	}

	if (Mode == EMode::Suppressed)
	{
		ActiveGuide = FPCGExSketchGuide();
		return;
	}

	if (Mode == EMode::Forced)
	{
		ActiveGuide = Candidates.IsValidIndex(ForcedIndex) ? Candidates[ForcedIndex] : FPCGExSketchGuide();
		return;
	}

	const UPCGExClusterSketchAuthoringSettings* Options = UPCGExClusterSketchAuthoringSettings::Get();
	if (!Options->bInferPlacementGuides)
	{
		ActiveGuide = FPCGExSketchGuide();
		return;
	}

	const double CaptureRadii = Options->GuideCaptureRadius;
	const double ReleaseRadii = CaptureRadii * PCGExSketchPlacement::ReleaseScale;

	// Acquisition only: once latched, sliding back toward the anchor keeps the guide. With no base point
	// the plane has already given up, and a guide is the only thing left that can answer -- so allow it.
	bool bMayAcquire = true;
	if (bHasBasePoint)
	{
		const double MinTravel = PCGExSketchPlacement::ScreenRadiusAt(LocalRay, Anchor) * PCGExSketchPlacement::MinTravelRadii;
		bMayAcquire = FVector::Dist(BasePoint, Anchor) >= MinTravel;
	}

	if (IncumbentIndex != INDEX_NONE && IncumbentScore <= ReleaseRadii)
	{
		// The incumbent still holds; only a clearly better rival takes it.
		if (BestIndex != INDEX_NONE && BestScore <= CaptureRadii && BestScore + PCGExSketchPlacement::SwitchMargin < IncumbentScore)
		{
			ActiveGuide = Candidates[BestIndex];
		}
		return;
	}

	ActiveGuide = (bMayAcquire && BestIndex != INDEX_NONE && BestScore <= CaptureRadii) ? Candidates[BestIndex] : FPCGExSketchGuide();
}

bool FPCGExSketchPlacementSolver::Resolve(const FRay& LocalRay, FVector& OutPoint)
{
	FVector BasePoint = FVector::ZeroVector;
	const bool bHasBasePoint = IntersectWorkPlane(LocalRay, BasePoint);

	UpdateActiveGuide(LocalRay, BasePoint, bHasBasePoint);

	// A guide is AUTHORITATIVE once it holds. When it cannot answer -- the ray runs down its length --
	// the caller keeps its previous point; falling through to the plane would silently slide the
	// placement off a line the user explicitly locked.
	if (ActiveGuide.IsValid())
	{
		FVector OnGuide = FVector::ZeroVector;
		double Distance = 0.0;
		if (!ClosestPointOnGuide(LocalRay, ActiveGuide, OnGuide, Distance))
		{
			return false;
		}
		OutPoint = OnGuide;
		return true;
	}

	if (bHasBasePoint)
	{
		OutPoint = BasePoint;
		return true;
	}
	return false;
}

double FPCGExSketchPlacementSolver::GetCaptureProximity(const int32 CandidateIndex) const
{
	if (!CandidateScores.IsValidIndex(CandidateIndex))
	{
		return 0.0;
	}

	const double Capture = UPCGExClusterSketchAuthoringSettings::Get()->GuideCaptureRadius;
	const double Falloff = Capture * PCGExSketchPlacement::PreviewFalloffScale;
	return FMath::Clamp(1.0 - (CandidateScores[CandidateIndex] - Capture) / FMath::Max(Falloff - Capture, UE_DOUBLE_SMALL_NUMBER), 0.0, 1.0);
}

void FPCGExSketchPlacementSolver::CycleGuide()
{
	if (Candidates.IsEmpty())
	{
		return;
	}

	// Advance from whatever is SHOWING, inference latch included, so Tab never jumps back to the top of
	// the list mid-gesture. IndexOfByKey yields INDEX_NONE with no guide, landing Next on 0. Wrapping
	// past the last candidate returns to inference, so cycling always has a way home.
	const int32 Next = (Mode == EMode::Forced ? ForcedIndex : Candidates.IndexOfByKey(ActiveGuide)) + 1;
	if (Next >= Candidates.Num())
	{
		ResetGuide();
		return;
	}

	Mode = EMode::Forced;
	ForcedIndex = Next;
	ActiveGuide = Candidates[Next];
}

bool FPCGExSketchPlacementSolver::ReleaseGuide()
{
	// Nothing showing means nothing to release: leave the mode alone so the key falls through to
	// whatever else it means, instead of silently suppressing inference for the rest of the gesture.
	if (!ActiveGuide.IsValid())
	{
		return false;
	}

	Mode = EMode::Suppressed;
	ForcedIndex = INDEX_NONE;
	ActiveGuide = FPCGExSketchGuide();
	return true;
}

void FPCGExSketchPlacementSolver::ResetGuide()
{
	Mode = EMode::Inferred;
	ForcedIndex = INDEX_NONE;
	ActiveGuide = FPCGExSketchGuide();
}
