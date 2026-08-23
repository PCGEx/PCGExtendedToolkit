// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchDrawHelper.h"

#include "SceneManagement.h"
#include "Sketch/PCGExClusterSketchAuthoringSettings.h"
#include "Sketch/PCGExClusterSketchComponent.h"
#include "Sketch/PCGExClusterSketchConstraint.h"
#include "Sketch/PCGExClusterSketchStyle.h"
#include "Sketch/PCGExSketchEditController.h"
#include "Sketch/PCGExSketchPlacement.h"

namespace PCGExSketchDrawHelper
{
	// Sizes stay local: they are immediate-mode screen-space quantities, not part of the project's
	// world-space style vocabulary. Every COLOR comes from the shared settings object, so the
	// standalone editor and the in-level mode restyle together.
	constexpr float VertexSize = 10.0f;
	constexpr float SelectedVertexSize = 14.0f;
	constexpr float HoverBonus = 4.0f;
	constexpr float EdgeThickness = 1.5f;
	constexpr float SelectedEdgeThickness = 3.0f;
	constexpr float SideEffectScale = 0.6f;
	constexpr double GhostRingRadius = 12.0;
	/** Ring around a vertex some constraint could not satisfy. */
	constexpr double UnsatisfiedRingRadius = 16.0;
	/** Phantom edge (an Along's anchor span): dotted, a touch sparser than a guide so the two read apart. */
	constexpr double PhantomDotSize = 3.0;
	constexpr double PhantomGapSize = 7.0;

	// A guide has to read as a LINE, not a tether, so it always overshoots the point it is placing.
	constexpr double GuideOvershoot = 1.5;
	constexpr double GuideMinReach = 100.0;

	// DOTTED, not dashed: the connect preview owns the dashed look, and two affordances that can be on
	// screen at once must not share a line style. Short mark, long gap.
	constexpr double GuideDotSize = 2.0;
	constexpr double GuideGapSize = 9.0;
	constexpr double CandidateDotSize = 1.5;
	constexpr double CandidateGapSize = 13.0;

	// Candidates sit on a short stub either side of the anchor; only the ACTIVE guide runs the full
	// length of the placement, and always overshoots them.
	constexpr double CandidateReachScale = 0.75;

	/**
	 * Independent mark and gap, which DrawDashedLine cannot express -- it hardcodes a 1:1 duty cycle,
	 * so shrinking its dash only ever yields a finer dash.
	 *
	 * DrawTranslucentLine, never DrawLine: the latter is documented to IGNORE Color.A (FBatchedElements
	 * forces it to 1), and guide opacity is what ranks a candidate against the one in force.
	 */
	void DrawDottedLine(FPrimitiveDrawInterface* PDI, const FVector& Start, const FVector& End, const FLinearColor& Color, const double DotSize, const double GapSize, const uint8 DepthPriority)
	{
		FVector Dir = End - Start;
		const double Length = Dir.Size();
		if (Length <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}
		Dir /= Length;

		const double Step = DotSize + GapSize;
		PDI->AddReserveLines(DepthPriority, FMath::CeilToInt32(Length / Step));

		for (double At = 0.0; At < Length; At += Step)
		{
			PDI->DrawTranslucentLine(Start + Dir * At, Start + Dir * FMath::Min(At + DotSize, Length), Color, DepthPriority);
		}
	}

	FLinearColor GuideColor(const UPCGExClusterSketchStyleSettings* Style, const EPCGExSketchGuideSource Source)
	{
		switch (Source)
		{
		case EPCGExSketchGuideSource::LatticeAxis:
			return Style->GuideAxisColor;
		case EPCGExSketchGuideSource::LatticeWalk:
			return Style->GuideWalkColor;
		case EPCGExSketchGuideSource::Complement:
			return Style->GuideComplementColor;
		case EPCGExSketchGuideSource::IncidentEdge:
			return Style->GuideEdgeColor;
		default:
			return Style->PreviewAffordanceColor;
		}
	}
}

void FPCGExSketchDrawHelper::Draw(const FPCGExSketchEditController& Controller, FPrimitiveDrawInterface* PDI)
{
	Draw(Controller, PDI, FMeshCoverage());
}

void FPCGExSketchDrawHelper::Draw(const FPCGExSketchEditController& Controller, FPrimitiveDrawInterface* PDI, const FMeshCoverage& InCoverage)
{
	const FPCGExClusterSketchModel* Model = Controller.GetTarget().GetModel();
	if (!Model || !PDI)
	{
		return;
	}

	FPCGExLatticeBasis Basis;
	const bool bHasBasis = Controller.GetBasis(Basis);
	const FTransform LocalToWorld = Controller.GetTarget().GetLocalToWorld();

	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();

	// Per-kind coverage comes from the host, which knows which STYLE is in force (preview and edit
	// configure different meshes) -- deciding it here from settings would guess the wrong one.
	const bool bMeshVertices = InCoverage.bVertices;
	const bool bMeshEdges = InCoverage.bEdges;
	// Hover colour is carried by the mesh layer too when it covers the kind; only a kind drawn here
	// still needs its hover expressed here.
	const bool bMeshHover = InCoverage.bHover;
	const bool bMeshGhosts = InCoverage.bGhosts;

	const FPCGExSketchHit& Hover = Controller.GetHover();
	const TSet<int32>& SelectedVertices = Controller.GetSelectedVertices();
	const TSet<int32>& SelectedEdges = Controller.GetSelectedEdges();

	// Vertex locations resolved once, coord-derived for bound vertices. Model space is kept alongside
	// world: the degeneracy test below measures against the model's own tolerance, and re-deriving a
	// location per vertex per EDGE is what made that test cost O(E x V) every frame.
	TArray<FVector> LocalLocations;
	TArray<FVector> Locations;
	LocalLocations.SetNumUninitialized(Model->Vertices.Num());
	Locations.SetNumUninitialized(Model->Vertices.Num());
	for (int32 i = 0; i < Model->Vertices.Num(); ++i)
	{
		LocalLocations[i] = FPCGExClusterSketchModel::ResolvedLocation(Model->Vertices[i], bHasBasis ? &Basis : nullptr);
		Locations[i] = LocalToWorld.TransformPosition(LocalLocations[i]);
	}

	// Invalid-state highlight: vertices RESOLVING to the same printed location (duplicate coords,
	// overlapping free verts, or a rank-collapsed basis stacking layers). Same key as the print
	// warning and the Merge Collocated cleanup, so all three agree.
	TSet<int32> Overlapping;
	{
		TMap<FVector, int32> FirstAtLocation;
		FirstAtLocation.Reserve(Locations.Num());
		for (int32 i = 0; i < Locations.Num(); ++i)
		{
			const FVector Key = PCGExSketch::QuantizedLocationKey(Locations[i]);
			if (const int32* First = FirstAtLocation.Find(Key))
			{
				Overlapping.Add(i);
				Overlapping.Add(*First);
			}
			else
			{
				FirstAtLocation.Add(Key, i);
			}
		}
	}

	// Basis tripod at the lattice origin -- a cheap "the snap model is live" cue.
	if (bHasBasis)
	{
		const FVector Origin = LocalToWorld.TransformPosition(Basis.Origin);
		for (int32 k = 0; k < Basis.NumAxes; ++k)
		{
			PDI->DrawLine(Origin, LocalToWorld.TransformPosition(Basis.Origin + Basis.AxisVecs[k]), Style->BasisColor, SDPG_Foreground, 0.5f);
		}
	}

	// Ghost crossings: an OFFER, never an automatic cut. Clicking one materializes its vertex and
	// splits both edges through it. Drawn hollow-ish and dim until hovered.
	TSet<int32> CrossingEdges;
	const TArray<FPCGExClusterSketchCrossing>& Crossings = Controller.GetCrossings();
	for (const FPCGExClusterSketchCrossing& Crossing : Crossings)
	{
		CrossingEdges.Add(Crossing.EdgeA);
		CrossingEdges.Add(Crossing.EdgeB);
	}

	// What the delete gesture would take if it fired right now -- the same query the mesh layer uses, so
	// both hosts advertise the same consequence.
	TSet<int32> DoomedVertices;
	TSet<int32> DoomedEdges;
	if (Controller.GetDeleteIntent() && Hover.IsVertex())
	{
		Model->GatherVertexRemovalCascade(Hover.Index, DoomedVertices, DoomedEdges);
	}

#if WITH_EDITORONLY_DATA
	// Constraints: the phantom span an Along rides (while its subject is selected, hovered or dragged),
	// and a warning ring on every vertex a constraint could not satisfy or no longer resolves.
	TSet<int32> UnsatisfiedVertices;
	if (!Model->Constraints.IsEmpty())
	{
		FPCGExSketchSolveContext Ctx;
		Ctx.Build(*Model, bHasBasis ? &Basis : nullptr, {});
		const TMap<uint32, FPCGExSketchConstraintResidual>& Residuals = Controller.GetConstraintResiduals();
		TArray<int32> Movable;

		for (const FInstancedStruct& Entry : Model->Constraints)
		{
			const FPCGExSketchConstraint* C = Entry.GetPtr<FPCGExSketchConstraint>();
			if (!C || !C->bEnabled)
			{
				continue;
			}

			const FPCGExSketchConstraintResidual* R = Residuals.Find(C->Id);
			if (R && !R->IsSatisfied())
			{
				Movable.Reset();
				C->GatherMovableVertices(Ctx, Movable);
				UnsatisfiedVertices.Append(Movable);
			}

			if (const FPCGExSketchConstraint_Along* Along = Entry.GetPtr<FPCGExSketchConstraint_Along>(); Along && Along->ResolvesIn(Ctx))
			{
				const int32 S = Ctx.VertexIndex(Along->Subjects[PCGExSketch::AlongRole::Subject]);
				const bool bLive = SelectedVertices.Contains(S)
					|| (Hover.IsVertex() && Hover.Index == S)
					|| Controller.GetDragVertex() == S;
				if (!bLive)
				{
					continue;
				}
				const FVector A = Locations[Ctx.VertexIndex(Along->Subjects[PCGExSketch::AlongRole::AnchorA])];
				const FVector B = Locations[Ctx.VertexIndex(Along->Subjects[PCGExSketch::AlongRole::AnchorB])];
				PCGExSketchDrawHelper::DrawDottedLine(PDI, A, B, Style->GuideEdgeColor, PCGExSketchDrawHelper::PhantomDotSize, PCGExSketchDrawHelper::PhantomGapSize, SDPG_Foreground);
			}
		}
	}
	for (const int32 i : UnsatisfiedVertices)
	{
		if (!Locations.IsValidIndex(i))
		{
			continue;
		}
		constexpr double Radius = PCGExSketchDrawHelper::UnsatisfiedRingRadius;
		DrawCircle(PDI, Locations[i], FVector::XAxisVector, FVector::YAxisVector, Style->MergeAffordanceColor, Radius, 16, SDPG_Foreground, 1.0f);
		DrawCircle(PDI, Locations[i], FVector::XAxisVector, FVector::ZAxisVector, Style->MergeAffordanceColor, Radius, 16, SDPG_Foreground, 1.0f);
	}
#endif

	// Edges under vertices.
	const double StubLength = (bHasBasis && Basis.NumAxes > 0) ? Basis.AxisVecs[0].Size() * 0.35 : 35.0;
	for (int32 e = 0; e < Model->Edges.Num(); ++e)
	{
		const FPCGExClusterSketchEdge& E = Model->Edges[e];
		if (!Locations.IsValidIndex(E.A) || !Locations.IsValidIndex(E.B))
		{
			// A DORMANT edge (endpoint index not materialized yet) would otherwise be invisible until a
			// new vertex silently activates it -- draw a warning stub from whichever endpoint exists.
			const int32 ValidEnd = Locations.IsValidIndex(E.A) ? E.A : (Locations.IsValidIndex(E.B) ? E.B : INDEX_NONE);
			if (ValidEnd != INDEX_NONE)
			{
				DrawDashedLine(PDI, Locations[ValidEnd], Locations[ValidEnd] + FVector(0, 0, StubLength), Style->MergeAffordanceColor, 5.0, SDPG_Foreground);
			}
			continue;
		}
		const bool bSelected = SelectedEdges.Contains(e);
		const bool bHovered = Hover.Type == FPCGExSketchHit::EType::Edge && Hover.Index == e;
		// Two different states, two different reads: an edge passing THROUGH a vertex is DEGENERATE
		// (a cluster cannot carry it) and shouts; an edge merely CROSSING another is legitimate, and
		// only whispers that a ghost is on offer. O(E x V), fine at sketch scale. While the host's
		// delete modifier is held, the hovered edge reads as a delete target.
		const bool bDegenerate = Model->FindVertexOnEdgeInterior(e, LocalLocations) != INDEX_NONE;
		FLinearColor Color = Style->EditEdge.Color;
		if (CrossingEdges.Contains(e))
		{
			Color = Style->EditVertexPhantom.Color;
		}
		if (bSelected)
		{
			Color = Style->SelectedColor;
		}
		if (bDegenerate)
		{
			Color = Style->MergeAffordanceColor;
		}
		if (DoomedEdges.Contains(e))
		{
			Color = Style->DeleteIntentColor;
		}
		if (bHovered)
		{
			Color = Controller.GetDeleteIntent() ? Style->DeleteIntentColor : Style->HoverColor;
		}
		// Meshes carry idle, selection, hover and the crossing tint; only DEGENERATE, which they have no
		// instance for, still shouts from here.
		if (bMeshEdges && (!bHovered || bMeshHover) && !bDegenerate)
		{
			continue;
		}
		PDI->DrawLine(Locations[E.A], Locations[E.B], Color, SDPG_Foreground, (bSelected || bHovered) ? PCGExSketchDrawHelper::SelectedEdgeThickness : PCGExSketchDrawHelper::EdgeThickness);
	}

	for (int32 i = 0; i < Model->Vertices.Num(); ++i)
	{
		const bool bSelected = SelectedVertices.Contains(i);
		const bool bHovered = Hover.Type == FPCGExSketchHit::EType::Vertex && Hover.Index == i;
		// Tool-inserted (side-effect) vertices read as secondary: dimmer and smaller than hand-made ones.
		bool bSideEffect = false;
#if WITH_EDITORONLY_DATA
		bSideEffect = Model->Vertices[i].bSideEffect;
#endif
		// Same precedence as the mesh layer, from the same settings: provenance outranks the bound tint.
		FLinearColor Color = Style->EditVertexIdle.Color;
		if (Model->Vertices[i].bLatticeBound)
		{
			Color = Style->EditVertexBoundColor;
		}
		if (bSideEffect)
		{
			Color = Style->EditVertexSideEffectColor;
		}
		if (bSelected)
		{
			Color = Style->SelectedColor;
		}
		if (Overlapping.Contains(i))
		{
			// Invalid state outranks selection; only aiming (doom, hover) overrides it.
			Color = Style->MergeAffordanceColor;
		}
		if (DoomedVertices.Contains(i))
		{
			Color = Style->DeleteIntentColor;
		}
		// Hover outranks selection, EXCEPT where the outline mesh already carries the hover signal -- a
		// selected vertex keeps reading as selected under the cursor. Mirrors the mesh layer's rule.
		if (bHovered && !(bSelected && bMeshHover))
		{
			Color = Controller.GetDeleteIntent() ? Style->DeleteIntentColor : Style->HoverColor;
		}
		float Size = bSelected ? PCGExSketchDrawHelper::SelectedVertexSize : PCGExSketchDrawHelper::VertexSize;
		if (bSideEffect && !bSelected)
		{
			Size *= PCGExSketchDrawHelper::SideEffectScale;
		}
		if (bMeshVertices && (!bHovered || bMeshHover) && !Overlapping.Contains(i))
		{
			continue;
		}
		PDI->DrawPoint(Locations[i], Color, Size + (bHovered ? PCGExSketchDrawHelper::HoverBonus : 0.0f), SDPG_Foreground);
	}

	// Ghost markers last, so they sit above the edges they offer to cut.
	for (int32 c = 0; c < Crossings.Num(); ++c)
	{
		const bool bHovered = Hover.Type == FPCGExSketchHit::EType::Crossing && Hover.Index == c;
		const FVector Pos = LocalToWorld.TransformPosition(Crossings[c].Location);
		const FLinearColor Color = bHovered ? Style->PreviewAffordanceColor : Style->EditVertexPhantom.Color;
		const float Size = bHovered ? PCGExSketchDrawHelper::SelectedVertexSize + PCGExSketchDrawHelper::HoverBonus : PCGExSketchDrawHelper::VertexSize;
		if (!bMeshGhosts)
		{
			PDI->DrawPoint(Pos, Color, Size, SDPG_Foreground);
		}
		if (bHovered && !(bMeshGhosts && bMeshHover))
		{
			// A hovered ghost reads as "click to place a vertex here": ring it.
			constexpr double Radius = PCGExSketchDrawHelper::GhostRingRadius;
			DrawCircle(PDI, Pos, FVector::XAxisVector, FVector::YAxisVector, Color, Radius, 16, SDPG_Foreground, 1.0f);
			DrawCircle(PDI, Pos, FVector::XAxisVector, FVector::ZAxisVector, Color, Radius, 16, SDPG_Foreground, 1.0f);
		}
	}

	// Placement guides, under the affordances they steer. Dotted throughout: the connect preview owns
	// the dashed look and the two can share the screen.
	if (Controller.HasPlacementPreview())
	{
		const FPCGExSketchPlacementSolver& Solver = Controller.GetPlacement();
		const FPCGExSketchGuide& Active = Solver.GetActiveGuide();
		const double CellReach = (bHasBasis && Basis.NumAxes > 0) ? LocalToWorld.TransformVector(Basis.AxisVecs[0]).Size() * 2.0 : PCGExSketchDrawHelper::GuideMinReach;

		// The rails the gesture COULD take, faint and short, brightening as the cursor swings toward one
		// so a capture announces itself before it happens.
		if (UPCGExClusterSketchAuthoringSettings::Get()->bShowGuideCandidates)
		{
			const TArray<FPCGExSketchGuide>& Candidates = Solver.GetCandidates();
			for (int32 g = 0; g < Candidates.Num(); ++g)
			{
				if (Candidates[g] == Active)
				{
					continue;
				}
				const FVector Origin = LocalToWorld.TransformPosition(Candidates[g].Origin);
				const FVector Dir = LocalToWorld.TransformVector(Candidates[g].Direction).GetSafeNormal() * (CellReach * PCGExSketchDrawHelper::CandidateReachScale);

				FLinearColor Color = PCGExSketchDrawHelper::GuideColor(Style, Candidates[g].Source);
				Color.A = FMath::Lerp(Style->GuideCandidateOpacity, 1.0f, static_cast<float>(Solver.GetCaptureProximity(g)));

				PCGExSketchDrawHelper::DrawDottedLine(PDI, Origin - Dir, Origin + Dir, Color,
				                                      PCGExSketchDrawHelper::CandidateDotSize, PCGExSketchDrawHelper::CandidateGapSize, SDPG_Foreground);
			}
		}

		if (Active.IsValid())
		{
			const FVector Origin = LocalToWorld.TransformPosition(Active.Origin);
			const FVector PlacedPos = LocalToWorld.TransformPosition(Controller.GetPlacementPoint());
			const FVector Dir = LocalToWorld.TransformVector(Active.Direction).GetSafeNormal();
			const double Reach = FMath::Max(CellReach, FVector::Dist(Origin, PlacedPos) * PCGExSketchDrawHelper::GuideOvershoot);

			PCGExSketchDrawHelper::DrawDottedLine(PDI, Origin - Dir * Reach, Origin + Dir * Reach,
			                                      PCGExSketchDrawHelper::GuideColor(Style, Active.Source),
			                                      PCGExSketchDrawHelper::GuideDotSize, PCGExSketchDrawHelper::GuideGapSize, SDPG_Foreground);
		}
	}

	// Where a Ctrl+click would land right now -- the add gesture's own ghost.
	if (Controller.HasAddPreview())
	{
		PDI->DrawPoint(LocalToWorld.TransformPosition(Controller.GetAddPreviewLocal()), Style->PreviewAffordanceColor, PCGExSketchDrawHelper::VertexSize, SDPG_Foreground);
	}

	// Drag affordances: move ghost or connect preview line.
	if (Controller.GetDragMode() == FPCGExSketchEditController::EDragMode::Connect && Locations.IsValidIndex(Controller.GetDragVertex()))
	{
		const FVector Start = Locations[Controller.GetDragVertex()];
		const int32 TargetVertex = Controller.GetDragTargetVertex();
		const FVector End = Locations.IsValidIndex(TargetVertex) ? Locations[TargetVertex] : LocalToWorld.TransformPosition(Controller.GetDragPreviewLocal());
		DrawDashedLine(PDI, Start, End, Style->PreviewAffordanceColor, 12.0, SDPG_Foreground);
		PDI->DrawPoint(End, Style->PreviewAffordanceColor, PCGExSketchDrawHelper::SelectedVertexSize, SDPG_Foreground);
	}
	else if (Controller.GetDragMode() == FPCGExSketchEditController::EDragMode::Move)
	{
		const int32 MergeCandidate = Controller.GetMergeCandidate();
		if (Locations.IsValidIndex(MergeCandidate))
		{
			// Release here MERGES the dragged vertex into this one -- loud, warm highlight.
			const FVector MergePos = Locations[MergeCandidate];
			PDI->DrawPoint(MergePos, Style->MergeAffordanceColor, PCGExSketchDrawHelper::SelectedVertexSize + 8.0f, SDPG_Foreground);
			DrawDashedLine(PDI, LocalToWorld.TransformPosition(Controller.GetDragPreviewLocal()), MergePos, Style->MergeAffordanceColor, 8.0, SDPG_Foreground);
		}
		else
		{
			PDI->DrawPoint(LocalToWorld.TransformPosition(Controller.GetDragPreviewLocal()), Style->PreviewAffordanceColor, PCGExSketchDrawHelper::VertexSize, SDPG_Foreground);
		}
	}

	// What the hand asked for, when the constraints made something else of it: a faint ghost of the
	// proposal tethered to where the element actually landed, so the input visibly registered.
	if (Controller.IsDragProposalDiverging())
	{
		const TConstArrayView<FVector> Proposal = Controller.GetDragProposal();
		FLinearColor Ghost = Style->PreviewAffordanceColor;
		Ghost.A = 0.5f;

		TArray<FVector, TInlineAllocator<2>> Landed;
		if (Controller.GetDragMode() == FPCGExSketchEditController::EDragMode::Move && Locations.IsValidIndex(Controller.GetDragVertex()))
		{
			Landed.Add(Locations[Controller.GetDragVertex()]);
		}
		else if (Controller.GetDragMode() == FPCGExSketchEditController::EDragMode::MoveEdge && Model->Edges.IsValidIndex(Controller.GetDragEdge()))
		{
			const FPCGExClusterSketchEdge& E = Model->Edges[Controller.GetDragEdge()];
			if (Locations.IsValidIndex(E.A) && Locations.IsValidIndex(E.B))
			{
				Landed.Add(Locations[E.A]);
				Landed.Add(Locations[E.B]);
			}
		}

		if (Landed.Num() == Proposal.Num())
		{
			for (int32 i = 0; i < Proposal.Num(); ++i)
			{
				const FVector P = LocalToWorld.TransformPosition(Proposal[i]);
				PDI->DrawPoint(P, Ghost, PCGExSketchDrawHelper::VertexSize, SDPG_Foreground);
				PCGExSketchDrawHelper::DrawDottedLine(PDI, P, Landed[i], Ghost, PCGExSketchDrawHelper::GuideDotSize, PCGExSketchDrawHelper::GuideGapSize, SDPG_Foreground);
			}
			if (Proposal.Num() == 2)
			{
				PDI->DrawTranslucentLine(LocalToWorld.TransformPosition(Proposal[0]), LocalToWorld.TransformPosition(Proposal[1]), Ghost, SDPG_Foreground, PCGExSketchDrawHelper::EdgeThickness);
			}
		}
	}
}

void FPCGExSketchDrawHelper::DrawWithComponent(const FPCGExSketchEditController& Controller, UPCGExClusterSketchComponent* Component, FPrimitiveDrawInterface* PDI)
{
	if (!Component)
	{
		Draw(Controller, PDI);
		return;
	}

	// Selection and hover live on the controller and change without notifying anyone, and a drag moves
	// geometry every frame without a completed-operation notify -- so the whole state is pushed here
	// rather than plumbed through callbacks. The component ignores an unchanged push.
	FPCGExClusterSketchEditState State;
	State.bActive = true;
	State.ModelRevision = Controller.GetModelRevision();
	State.SelectedVertices = Controller.GetSelectedVertices();
	State.SelectedEdges = Controller.GetSelectedEdges();
	State.bDeleteIntent = Controller.GetDeleteIntent();

	const FPCGExSketchHit& Hover = Controller.GetHover();
	State.HoveredVertex = Hover.Type == FPCGExSketchHit::EType::Vertex ? Hover.Index : INDEX_NONE;
	State.HoveredEdge = Hover.Type == FPCGExSketchHit::EType::Edge ? Hover.Index : INDEX_NONE;
	State.HoveredGhost = Hover.Type == FPCGExSketchHit::EType::Crossing ? Hover.Index : INDEX_NONE;

	// Crossings are the controller's to compute, but the mesh layer expresses them: ghost markers in the
	// phantom style, and the two participating edges tinted to match. Locations are model-local, which
	// is the component's own frame.
	const TArray<FPCGExClusterSketchCrossing>& Crossings = Controller.GetCrossings();
	State.GhostLocations.Reserve(Crossings.Num());
	for (const FPCGExClusterSketchCrossing& Crossing : Crossings)
	{
		State.GhostLocations.Add(Crossing.Location);
		State.CrossingEdges.Add(Crossing.EdgeA);
		State.CrossingEdges.Add(Crossing.EdgeB);
	}

	Component->SetEditState(State);

	FMeshCoverage Coverage;
	Coverage.bVertices = Component->DrawsVerticesAsMesh();
	Coverage.bEdges = Component->DrawsEdgesAsMesh();
	Coverage.bHover = Component->DrawsHoverAsMesh();
	Coverage.bGhosts = Component->DrawsGhostsAsMesh();
	Draw(Controller, PDI, Coverage);
}
