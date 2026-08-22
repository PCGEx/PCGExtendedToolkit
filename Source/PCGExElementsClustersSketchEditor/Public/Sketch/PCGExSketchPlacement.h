// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
// CoreMinimal carries only MathFwd's FRay alias; every ray here is dereferenced, and the controller
// stores one by value.
#include "Math/Ray.h"

struct FPCGExClusterSketchModel;
struct FPCGExLatticeBasis;

namespace PCGExSketchPlacement
{
	/**
	 * Screen-constant world radius at a point: a world radius that grows with distance, so an element
	 * keeps a steady footprint on screen. THE definition, shared by hit-testing and guide capture --
	 * the two must never disagree about how big something is.
	 */
	PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API double ScreenRadiusAt(const FRay& LocalRay, const FVector& LocalPos, double InMinWorldRadius = 0.0);

	/** Placement distance along the ray when nothing else can answer -- a fresh add has no previous
	 *  point to keep. */
	inline constexpr double FallbackPlaceDistance = 500.0;
}

/** Where a guide came from: its colour, its cycle order, and the option that gates it. */
enum class EPCGExSketchGuideSource : uint8
{
	None,
	LatticeAxis,
	Complement,
	IncidentEdge,
	LatticeWalk,
};

/**
 * One placement guide: an infinite LINE through Origin along Direction.
 *
 * Guides are only ever lines. A plane candidate resolves to a point ON the cursor ray, so it would
 * score zero distance and beat every line -- the work plane is the fallback placement, never a
 * competitor for it.
 */
struct PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchGuide
{
	EPCGExSketchGuideSource Source = EPCGExSketchGuideSource::None;

	FVector Origin = FVector::ZeroVector;

	/** Unit length. */
	FVector Direction = FVector::ForwardVector;

	/** Position within the generator that produced it. With Source this is the guide's IDENTITY: the
	 *  latch keys on the pair rather than on an array slot, so rebuilding candidates never drops it. */
	int32 SourceIndex = INDEX_NONE;

	bool IsValid() const { return Source != EPCGExSketchGuideSource::None; }

	bool operator==(const FPCGExSketchGuide& Other) const { return Source == Other.Source && SourceIndex == Other.SourceIndex; }
};

/**
 * Resolves a cursor ray to a placement point for every sketch gesture -- add, move and connect all come
 * through here, so the three cannot drift apart on what "where the cursor points" means.
 *
 * Base placement is the WORK PLANE through the gesture anchor: the lattice plane where the basis spans
 * one, else Z-up. Not a screen-facing plane, which carries no depth intent and maps a small cursor move
 * to a huge world displacement at grazing view angles. Seen edge-on, though, a work plane answers
 * nothing at all -- an orthographic Front view of a Z-up one is exactly parallel to the ray -- so there,
 * and only there, the view plane stands in.
 *
 * GUIDES sit on top of that -- lattice directions, the out-of-plane complement, the anchor's own edges
 * -- and capture the placement when the cursor runs near one. Capture is a Schmitt trigger (wider
 * release than capture radius, plus a margin a rival must beat) so a guide cannot flicker, and nothing
 * may capture until the cursor has travelled clear of the anchor, where every guide converges.
 *
 * Snapping runs AFTER this, untouched: a guide steers where the cursor resolves to, never whether the
 * result lands on the lattice.
 */
struct PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchPlacementSolver
{
	/** How the active guide is chosen. */
	enum class EMode : uint8
	{
		/** Inference picks it, with hysteresis. */
		Inferred,
		/** Cycled to by hand; inference stays off until cycling wraps back round. */
		Forced,
		/** Released by hand; no guide at all until the next cycle. */
		Suppressed,
	};

	/** Start a gesture anchored at InAnchor. Drops the latch and any manual override, and fixes the work
	 *  plane for the gesture's duration. */
	void BeginGesture(const FVector& InAnchor, const FPCGExLatticeBasis* Basis);

	/**
	 * (Re)build the candidate set. Idempotent and cheap enough to run per frame -- the latch keys on
	 * guide identity, not on array position, so a rebuild keeps whatever was captured.
	 * @param bPointWillSnap whether the resolved point will be quantized onto the lattice afterwards;
	 *        the complement guide is withheld when it will, since snapping discards that direction.
	 */
	void BuildCandidates(const FPCGExClusterSketchModel* Model, int32 AnchorVertex, const FPCGExLatticeBasis* Basis, bool bPointWillSnap);

	/**
	 * Resolve a ray to a placement point. A held guide is AUTHORITATIVE -- when it cannot answer,
	 * because the ray runs down its length, this fails rather than sliding the placement off it.
	 * @return false when nothing could answer; the caller must then keep whatever point it last had.
	 */
	bool Resolve(const FRay& LocalRay, FVector& OutPoint);

	/** Step to the next candidate; past the last one hands control back to inference. */
	void CycleGuide();

	/** Drop to no guide until the next cycle. @return true if there was one to drop. */
	bool ReleaseGuide();

	/** Forget the latch and any override, keeping the anchor and work plane. */
	void ResetGuide();

	const FPCGExSketchGuide& GetActiveGuide() const { return ActiveGuide; }
	const FVector& GetAnchor() const { return Anchor; }
	const TArray<FPCGExSketchGuide>& GetCandidates() const { return Candidates; }

	/**
	 * How close the cursor is to capturing a candidate: 0 at the edge of visibility, 1 once it is at
	 * capture distance or nearer. Refreshed by every Resolve, in EVERY mode, so a candidate preview
	 * telegraphs a capture before it happens even while a guide is forced or suppressed.
	 */
	double GetCaptureProximity(int32 CandidateIndex) const;

private:
	/** Closest point ON a guide to the ray, and their separation. False when the two are parallel or the
	 *  meeting lies behind the ray origin. */
	static bool ClosestPointOnGuide(const FRay& LocalRay, const FPCGExSketchGuide& Guide, FVector& OutPoint, double& OutDistance);

	/** Ray against the gesture's work plane, or against the VIEW plane when that is edge-on to the ray
	 *  (an orthographic side view of it). False only when the resulting plane lies behind the ray. */
	bool IntersectWorkPlane(const FRay& LocalRay, FVector& OutPoint) const;

	void UpdateActiveGuide(const FRay& LocalRay, const FVector& BasePoint, bool bHasBasePoint);

	/** Appends a guide if its direction is usable and not already present (a line has no sign, so an
	 *  antipodal duplicate is the same guide). */
	void AddCandidate(EPCGExSketchGuideSource InSource, int32 InSourceIndex, const FVector& InDirection);

	TArray<FPCGExSketchGuide> Candidates;

	/** Per-candidate ray distance in SCREEN RADII, parallel to Candidates. Huge where the candidate is
	 *  parallel to the ray or behind it. */
	TArray<double> CandidateScores;

	FPCGExSketchGuide ActiveGuide;

	FVector Anchor = FVector::ZeroVector;
	FVector PlaneNormal = FVector::UpVector;

	EMode Mode = EMode::Inferred;

	/** Index into Candidates while Mode is Forced. */
	int32 ForcedIndex = INDEX_NONE;
};
