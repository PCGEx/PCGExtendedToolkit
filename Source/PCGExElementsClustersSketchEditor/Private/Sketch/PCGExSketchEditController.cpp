// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchEditController.h"

#include "ScopedTransaction.h"
#include "Helpers/PCGExObjectNotifyHelpers.h"
#include "Misc/TransactionObjectEvent.h"
#include "UObject/UnrealType.h"
#include "Sketch/PCGExClusterSketch.h"
#include "Sketch/PCGExClusterSketchAuthoringSettings.h"
#include "Sketch/PCGExClusterSketchStyle.h"

#define LOCTEXT_NAMESPACE "PCGExSketchEditController"

namespace PCGExSketchEditController
{
	// Vertices win over edges when both are within reach; the factor keeps a vertex pickable at the
	// junction of its own edges.
	constexpr double EdgePickFactor = 0.75;

	// A gesture re-anchors -- dropping its guide latch -- once its anchor moves further than this.
	constexpr double AnchorDriftTolerance = 0.01;

	// A connect drag only latches the vertex under the pointer at a TIGHTER radius than picking:
	// hover-radius latching connected vertices the user never aimed at (screen-space generous AND
	// depth-ambiguous along the ray).
	constexpr double ConnectHoverTightFactor = 0.5;
}

#pragma region FPCGExSketchAssetEditTarget

FPCGExSketchAssetEditTarget::FPCGExSketchAssetEditTarget(UPCGExClusterSketch* InSketch)
	: Sketch(InSketch)
{
}

FText IPCGExSketchEditTarget::GetReadOnlyReason() const
{
	return NSLOCTEXT("PCGExSketchEditTarget", "ReadOnlyGeneric", "Read-only: this host does not author its own sketch.");
}

void IPCGExSketchEditTarget::BeginAuthoring()
{
	if (UObject* Host = GetTransactionObject())
	{
		Host->Modify();
	}
}

FPCGExClusterSketchModel* FPCGExSketchAssetEditTarget::GetModel()
{
	UPCGExClusterSketch* Pinned = Sketch.Get();
	return Pinned ? &Pinned->Model : nullptr;
}

const FPCGExClusterSketchModel* FPCGExSketchAssetEditTarget::GetModel() const
{
	const UPCGExClusterSketch* Pinned = Sketch.Get();
	return Pinned ? &Pinned->Model : nullptr;
}

const UPCGExClusterSnapProvider* FPCGExSketchAssetEditTarget::GetSnapProvider() const
{
	const UPCGExClusterSketch* Pinned = Sketch.Get();
	return Pinned ? Pinned->SnapProvider.Get() : nullptr;
}

bool FPCGExSketchAssetEditTarget::CanEdit() const
{
	return Sketch.IsValid();
}

UObject* FPCGExSketchAssetEditTarget::GetTransactionObject()
{
	return Sketch.Get();
}

bool FPCGExSketchAssetEditTarget::OwnsObject(const UObject* InObject) const
{
	const UPCGExClusterSketch* Pinned = Sketch.Get();
	// Inners too: the snap provider and decorators transact as their own objects.
	return Pinned && InObject && (InObject == Pinned || InObject->IsIn(Pinned));
}

void FPCGExSketchAssetEditTarget::NotifyChanged()
{
	// Same notification pair as the component host, so a sketch reaches downstream consumers identically
	// whichever host carries it. The viewport itself is realtime and reads the model every frame.
	if (UPCGExClusterSketch* Pinned = Sketch.Get())
	{
		PCGExEditor::NotifyObjectChanged(Pinned);
	}
}

#pragma endregion

#pragma region FPCGExSketchEditController

FPCGExSketchEditController::FPCGExSketchEditController(const TSharedRef<IPCGExSketchEditTarget>& InTarget)
	: Target(InTarget)
{
	// Host -> controller. The host never knows its controllers, so the engine's object-level delegates
	// are the channel; OwnsObject is the filter.
	TransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FPCGExSketchEditController::OnObjectTransacted);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FPCGExSketchEditController::OnObjectPropertyChanged);
}

FPCGExSketchEditController::~FPCGExSketchEditController()
{
	FCoreUObjectDelegates::OnObjectTransacted.Remove(TransactedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	CancelDrag();
}

void FPCGExSketchEditController::OnObjectTransacted(UObject* InObject, const FTransactionObjectEvent& InEvent)
{
	if (InEvent.GetEventType() == ETransactionObjectEventType::UndoRedo && Target->OwnsObject(InObject))
	{
		NotifyExternalChange();
	}
}

void FPCGExSketchEditController::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent)
{
	// Interactive edits (a slider mid-drag) are skipped: only the committed value matters here, and a
	// per-tick O(E^2) crossing sweep is not free.
	if (!bNotifying && InEvent.ChangeType != EPropertyChangeType::Interactive && Target->OwnsObject(InObject))
	{
		NotifyExternalChange();
	}
}

bool FPCGExSketchEditController::GetBasis(FPCGExLatticeBasis& OutBasis) const
{
	const UPCGExClusterSnapProvider* Provider = Target->GetSnapProvider();
	return Provider ? Provider->BuildBasis(OutBasis) : false;
}

const FPCGExClusterSketchModel* FPCGExSketchEditController::GetReadModel() const
{
	const IPCGExSketchEditTarget& ConstTarget = Target.Get();
	return ConstTarget.GetModel();
}

void FPCGExSketchEditController::SelectVertex(const int32 VertexIndex)
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model || !Model->Vertices.IsValidIndex(VertexIndex))
	{
		return;
	}
	SelectedVertexIds.Add(Model->Vertices[VertexIndex].Id);
	SelectedVertices.Add(VertexIndex);
	LastSelectedVertexId = Model->Vertices[VertexIndex].Id;
	LastSelectedVertex = VertexIndex;
}

void FPCGExSketchEditController::SelectEdge(const int32 EdgeIndex)
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model || !Model->Edges.IsValidIndex(EdgeIndex))
	{
		return;
	}
	SelectedEdgeIds.Add(Model->Edges[EdgeIndex].Id);
	SelectedEdges.Add(EdgeIndex);
}

void FPCGExSketchEditController::DeselectVertex(const int32 VertexIndex)
{
	if (const FPCGExClusterSketchModel* Model = GetReadModel(); Model && Model->Vertices.IsValidIndex(VertexIndex))
	{
		SelectedVertexIds.Remove(Model->Vertices[VertexIndex].Id);
	}
	SelectedVertices.Remove(VertexIndex);
	if (LastSelectedVertex == VertexIndex)
	{
		LastSelectedVertex = INDEX_NONE;
		LastSelectedVertexId = PCGExSketch::InvalidElementId;
	}
}

void FPCGExSketchEditController::DeselectEdge(const int32 EdgeIndex)
{
	if (const FPCGExClusterSketchModel* Model = GetReadModel(); Model && Model->Edges.IsValidIndex(EdgeIndex))
	{
		SelectedEdgeIds.Remove(Model->Edges[EdgeIndex].Id);
	}
	SelectedEdges.Remove(EdgeIndex);
}

void FPCGExSketchEditController::ResolveSelectionIndices()
{
	SelectedVertices.Reset();
	SelectedEdges.Reset();
	LastSelectedVertex = INDEX_NONE;

	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		SelectedVertexIds.Reset();
		SelectedEdgeIds.Reset();
		LastSelectedVertexId = PCGExSketch::InvalidElementId;
		return;
	}

	// One pass over the model, not one FindVertexIndex per id.
	for (int32 i = 0; i < Model->Vertices.Num(); ++i)
	{
		const uint32 Id = Model->Vertices[i].Id;
		if (SelectedVertexIds.Contains(Id))
		{
			SelectedVertices.Add(i);
			if (Id == LastSelectedVertexId)
			{
				LastSelectedVertex = i;
			}
		}
	}
	for (int32 e = 0; e < Model->Edges.Num(); ++e)
	{
		if (SelectedEdgeIds.Contains(Model->Edges[e].Id))
		{
			SelectedEdges.Add(e);
		}
	}

	// Ids the model no longer carries leave the selection for good.
	if (SelectedVertices.Num() != SelectedVertexIds.Num())
	{
		SelectedVertexIds.Reset();
		for (const int32 i : SelectedVertices)
		{
			SelectedVertexIds.Add(Model->Vertices[i].Id);
		}
	}
	if (SelectedEdges.Num() != SelectedEdgeIds.Num())
	{
		SelectedEdgeIds.Reset();
		for (const int32 e : SelectedEdges)
		{
			SelectedEdgeIds.Add(Model->Edges[e].Id);
		}
	}
	if (LastSelectedVertex == INDEX_NONE)
	{
		LastSelectedVertexId = PCGExSketch::InvalidElementId;
	}
}

FRay FPCGExSketchEditController::ToLocal(const FRay& WorldRay) const
{
	const FTransform WorldToLocal = Target->GetLocalToWorld().Inverse();
	// GetSafeNormal absorbs a scaled host transform; distances then measure in model space, which is
	// what the pick radii and snap operate in.
	return FRay(WorldToLocal.TransformPosition(WorldRay.Origin), WorldToLocal.TransformVector(WorldRay.Direction).GetSafeNormal());
}

double FPCGExSketchEditController::VertexPickFloor()
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	return Style->EditVertexIdle.Mesh.IsNull() ? 0.0 : Style->EditVertexIdle.Size;
}

double FPCGExSketchEditController::GhostPickFloor()
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	return Style->EditVertexPhantom.Mesh.IsNull() ? 0.0 : Style->EditVertexPhantom.Size;
}

double FPCGExSketchEditController::EdgePickFloor()
{
	const UPCGExClusterSketchStyleSettings* Style = UPCGExClusterSketchStyleSettings::Get();
	return Style->EditEdge.Mesh.IsNull() ? 0.0 : Style->EditEdge.Size;
}

double FPCGExSketchEditController::PickRadiusAt(const FRay& LocalRay, const FVector& LocalPos, const double InMinWorldRadius) const
{
	return PCGExSketchPlacement::ScreenRadiusAt(LocalRay, LocalPos, InMinWorldRadius);
}

FVector FPCGExSketchEditController::VertexLocation(const FPCGExClusterSketchVertex& V, const FPCGExLatticeBasis* Basis) const
{
	return FPCGExClusterSketchModel::ResolvedLocation(V, Basis);
}

FPCGExSketchHit FPCGExSketchEditController::HitTest(const FRay& WorldRay) const
{
	const FRay LocalRay = ToLocal(WorldRay);
	FPCGExSketchHit Hit = HitTestLocal(LocalRay);

	// ToLocal renormalizes, so RayT comes back in MODEL space. Both consumers -- the ITF router's
	// hit-depth arbitration and the mode's nearest-hit comparison -- read it as a WORLD depth, and a
	// scaled host would otherwise outbid or lose against every other behaviour by its scale factor.
	if (Hit.IsHit())
	{
		Hit.RayT *= Target->GetLocalToWorld().TransformVector(LocalRay.Direction).Size();
	}
	return Hit;
}

FPCGExSketchHit FPCGExSketchEditController::HitTestLocal(const FRay& LocalRay) const
{
	FPCGExSketchHit Hit;

	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		return Hit;
	}

	FPCGExLatticeBasis Basis;
	const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;

	// Vertices first -- nearest hit along the ray among those within pick radius.
	double BestT = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Model->Vertices.Num(); ++i)
	{
		const FVector Pos = VertexLocation(Model->Vertices[i], BasisPtr);
		const double T = FMath::Max(0.0, FVector::DotProduct(Pos - LocalRay.Origin, LocalRay.Direction));
		const double Dist = FVector::Dist(LocalRay.Origin + LocalRay.Direction * T, Pos);
		if (Dist <= PickRadiusAt(LocalRay, Pos, VertexPickFloor()) && T < BestT)
		{
			BestT = T;
			Hit.Type = FPCGExSketchHit::EType::Vertex;
			Hit.Index = i;
			Hit.RayT = T;
		}
	}
	if (Hit.IsHit())
	{
		return Hit;
	}

	// Ghost crossings next: they sit ON two edges, so they must out-rank them.
	for (int32 c = 0; c < Crossings.Num(); ++c)
	{
		const FVector Pos = Crossings[c].Location;
		const double T = FMath::Max(0.0, FVector::DotProduct(Pos - LocalRay.Origin, LocalRay.Direction));
		const double Dist = FVector::Dist(LocalRay.Origin + LocalRay.Direction * T, Pos);
		if (Dist <= PickRadiusAt(LocalRay, Pos, GhostPickFloor()) && T < BestT)
		{
			BestT = T;
			Hit.Type = FPCGExSketchHit::EType::Crossing;
			Hit.Index = c;
			Hit.RayT = T;
		}
	}
	if (Hit.IsHit())
	{
		return Hit;
	}

	// Edges last, against a long ray segment.
	const FVector RayEnd = LocalRay.Origin + LocalRay.Direction * 1.0e7;
	for (int32 e = 0; e < Model->Edges.Num(); ++e)
	{
		const FPCGExClusterSketchEdge& E = Model->Edges[e];
		if (!Model->Vertices.IsValidIndex(E.A) || !Model->Vertices.IsValidIndex(E.B))
		{
			continue;
		}
		const FVector A = VertexLocation(Model->Vertices[E.A], BasisPtr);
		const FVector B = VertexLocation(Model->Vertices[E.B], BasisPtr);
		FVector OnRay, OnSegment;
		FMath::SegmentDistToSegmentSafe(LocalRay.Origin, RayEnd, A, B, OnRay, OnSegment);
		const double T = FVector::Dist(LocalRay.Origin, OnRay);
		const double EdgeReach = FMath::Max(PickRadiusAt(LocalRay, OnSegment) * PCGExSketchEditController::EdgePickFactor, EdgePickFloor());
		if (FVector::Dist(OnRay, OnSegment) <= EdgeReach && T < BestT)
		{
			BestT = T;
			Hit.Type = FPCGExSketchHit::EType::Edge;
			Hit.Index = e;
			Hit.RayT = T;
		}
	}
	return Hit;
}

void FPCGExSketchEditController::UpdateHover(const FRay& WorldRay)
{
	// Refreshed here so ghosts stay honest after edits the controller never saw (details panel, undo),
	// but gated on a shape fingerprint: the sweep is O(E^2) and hovering cannot change the model.
	// A crossing that goes stale between fingerprints is harmless -- MaterializeCrossing re-derives it.
	RefreshCrossings();

	// Connect-drag targeting lives in UpdateDrag, not here -- hover alone is too loose to pick a
	// link target from.
	const FRay LocalRay = ToLocal(WorldRay);
	Hover = HitTestLocal(LocalRay);

	LastLocalRay = LocalRay;
	bHasLastLocalRay = true;

	// Add-intent preview, so the guide an add would take is visible BEFORE the click commits it. Not
	// while dragging: the drag owns the solver then.
	if (DragMode == EDragMode::None)
	{
		if (bAddIntent && !Hover.IsHit())
		{
			FPCGExLatticeBasis Basis;
			const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;
			AddPreviewLocal = ResolveAddPlacement(LocalRay, BasisPtr).Point;
			bHasAddPreview = true;
		}
		else
		{
			bHasAddPreview = false;
		}
	}
}

void FPCGExSketchEditController::ClearHover()
{
	Hover = FPCGExSketchHit();
	bHasAddPreview = false;
	if (DragMode == EDragMode::None)
	{
		PlacementGesture = EPlacementGesture::None;
		Placement.ResetGuide();
	}
}

void FPCGExSketchEditController::HandleClick(const FRay& WorldRay, const bool bAdditive, const bool bAddOnEmpty)
{
	ResolveSelectionIndices();

	const FPCGExSketchHit Hit = HitTest(WorldRay);

	if (!Hit.IsHit())
	{
		if (bAddOnEmpty)
		{
			AddVertexAtRay(WorldRay);
		}
		else if (!bAdditive)
		{
			ClearSelection();
			NotifyModelChanged();
		}
		return;
	}

	if (Hit.IsCrossing())
	{
		// The ghost is an OFFER: clicking it commits the cut. Nothing else materializes crossings.
		MaterializeCrossingAtRay(WorldRay);
		return;
	}

	if (Hit.IsVertex())
	{
		if (bAdditive && SelectedVertices.Contains(Hit.Index))
		{
			DeselectVertex(Hit.Index);
		}
		else
		{
			if (!bAdditive)
			{
				ClearSelection();
			}
			SelectVertex(Hit.Index);
		}
	}
	else
	{
		if (bAdditive && SelectedEdges.Contains(Hit.Index))
		{
			DeselectEdge(Hit.Index);
		}
		else
		{
			if (!bAdditive)
			{
				ClearSelection();
			}
			SelectEdge(Hit.Index);
		}
	}
	NotifyModelChanged();
}

void FPCGExSketchEditController::BeginDrag(const FRay& WorldRay, const bool bConnect)
{
	CancelDrag();
	ResolveSelectionIndices();

	const FRay LocalRay = ToLocal(WorldRay);
	const FPCGExSketchHit Hit = HitTestLocal(LocalRay);
	if (!Hit.IsVertex() && !Hit.IsEdge())
	{
		return;
	}

	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return;
	}

	bDragHasBasis = GetBasis(DragBasis);
	const FPCGExLatticeBasis* BasisPtr = bDragHasBasis ? &DragBasis : nullptr;

	bHasAddPreview = false;
	LastLocalRay = LocalRay;
	bHasLastLocalRay = true;
	DragProposalLocal.Reset();

	if (Hit.IsEdge())
	{
		// Connect has no meaning from an edge; a press on one always moves it, whatever the modifier.
		const FPCGExClusterSketchEdge& E = Model->Edges[Hit.Index];
		if (!Model->Vertices.IsValidIndex(E.A) || !Model->Vertices.IsValidIndex(E.B))
		{
			return;
		}
		DragMode = EDragMode::MoveEdge;
		DragEdgeIndex = Hit.Index;
		DragVertexIndex = INDEX_NONE;
		DragTargetVertexIndex = INDEX_NONE;
		MergeCandidateVertex = INDEX_NONE;
		DragEdgeStartA = VertexLocation(Model->Vertices[E.A], BasisPtr);
		DragEdgeStartB = VertexLocation(Model->Vertices[E.B], BasisPtr);
		DragPreviewLocal = (DragEdgeStartA + DragEdgeStartB) * 0.5;

		// Anchored at the midpoint: the edge's own line is then a candidate guide, and the work plane
		// passes through the grab.
		EnsurePlacementGesture(EPlacementGesture::Move, INDEX_NONE, DragPreviewLocal, BasisPtr, false);
		FVector Start;
		DragStartPoint = Placement.Resolve(LocalRay, Start) ? Start : DragPreviewLocal;

		ActiveTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveEdge", "Move Sketch Edge"));
		Target->BeginAuthoring();
		if (!SelectedEdges.Contains(DragEdgeIndex))
		{
			ClearSelection();
			SelectEdge(DragEdgeIndex);
		}
		return;
	}

	DragMode = bConnect ? EDragMode::Connect : EDragMode::Move;
	DragVertexIndex = Hit.Index;
	DragEdgeIndex = INDEX_NONE;
	DragTargetVertexIndex = INDEX_NONE;
	MergeCandidateVertex = INDEX_NONE;
	DragPreviewLocal = VertexLocation(Model->Vertices[Hit.Index], BasisPtr);

	// A bound vertex snaps regardless of the toggle, so its complement guide would be dead either way.
	const bool bWillSnap = BasisPtr && (bSnapEnabled || Model->Vertices[Hit.Index].bLatticeBound);
	EnsurePlacementGesture(
		DragMode == EDragMode::Move ? EPlacementGesture::Move : EPlacementGesture::Connect,
		DragVertexIndex, DragPreviewLocal, BasisPtr, bWillSnap);

	if (DragMode == EDragMode::Move)
	{
		// One transaction spans the whole drag; Modify() snapshots the pre-drag state exactly once.
		ActiveTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveVertex", "Move Sketch Vertex"));
		Target->BeginAuthoring();
		if (!SelectedVertices.Contains(DragVertexIndex))
		{
			ClearSelection();
			SelectVertex(DragVertexIndex);
		}
	}
	// Connect previews only; its transaction opens at release, when something actually mutates.
}

void FPCGExSketchEditController::UpdateDrag(const FRay& WorldRay)
{
	if (DragMode == EDragMode::None)
	{
		return;
	}

	UpdateHover(WorldRay);
	ApplyDrag(ToLocal(WorldRay));
}

void FPCGExSketchEditController::ApplyDrag(const FRay& LocalRay)
{
	LastLocalRay = LocalRay;
	bHasLastLocalRay = true;

	const FPCGExLatticeBasis* BasisPtr = bDragHasBasis ? &DragBasis : nullptr;
	DragProposalLocal.Reset();

	if (DragMode == EDragMode::MoveEdge)
	{
		FPCGExClusterSketchModel* Model = Target->GetModel();
		if (!Model || !Model->Edges.IsValidIndex(DragEdgeIndex))
		{
			return;
		}
		const int32 A = Model->Edges[DragEdgeIndex].A;
		const int32 B = Model->Edges[DragEdgeIndex].B;
		if (!Model->Vertices.IsValidIndex(A) || !Model->Vertices.IsValidIndex(B))
		{
			return;
		}

		EnsurePlacementGesture(EPlacementGesture::Move, INDEX_NONE, PlacementAnchor, BasisPtr, false);
		FVector Resolved;
		if (!Placement.Resolve(LocalRay, Resolved))
		{
			return; // unresolvable ray: keep the previous frame
		}
		const FVector Delta = Resolved - DragStartPoint;

		// Both endpoints receive the same delta; each then snaps by its own rule.
		DragProposalLocal.Add(DragEdgeStartA + Delta);
		DragProposalLocal.Add(DragEdgeStartB + Delta);
		const FVector CommittedA = CommitProposedLocation(A, DragProposalLocal[0], BasisPtr);
		const FVector CommittedB = CommitProposedLocation(B, DragProposalLocal[1], BasisPtr);
		++ModelRevision;

		// A directly constrained endpoint re-parameterises its constraint from the proposal (an Along
		// slides), so the solve lands it where the hand went rather than where it was.
		Model->AbsorbProposal(Model->Vertices[A].Id, CommittedA, BasisPtr);
		Model->AbsorbProposal(Model->Vertices[B].Id, CommittedB, BasisPtr);

		// The constraints reshape the proposal; a held endpoint only yields to a constraint naming it directly.
		TArray<uint32> Pins;
		GatherDragPins({A, B}, Pins);
		SolveConstraints(BasisPtr, Pins);
		DragPreviewLocal = (VertexLocation(Model->Vertices[A], BasisPtr) + VertexLocation(Model->Vertices[B], BasisPtr)) * 0.5;
		return;
	}

	// Re-anchoring is suppressed by passing the gesture's own anchor back; this only refreshes the
	// candidates against the current options and snap state.
	const FPCGExClusterSketchModel* ReadModel = GetReadModel();
	const bool bBound = ReadModel && ReadModel->Vertices.IsValidIndex(DragVertexIndex) && ReadModel->Vertices[DragVertexIndex].bLatticeBound;
	EnsurePlacementGesture(
		DragMode == EDragMode::Move ? EPlacementGesture::Move : EPlacementGesture::Connect,
		DragVertexIndex, PlacementAnchor, BasisPtr, BasisPtr && (bSnapEnabled || bBound));

	// An unresolvable ray keeps the previous preview instead of shooting the vertex off.
	FVector Resolved = FVector::ZeroVector;
	if (Placement.Resolve(LocalRay, Resolved))
	{
		DragPreviewLocal = Resolved;
	}

	if (bSnapEnabled && BasisPtr)
	{
		DragPreviewLocal = BasisPtr->CoordToWorld(BasisPtr->SnapWorldToCoord(DragPreviewLocal));
	}

	if (DragMode == EDragMode::Move)
	{
		FPCGExClusterSketchModel* Model = Target->GetModel();
		if (!Model || !Model->Vertices.IsValidIndex(DragVertexIndex))
		{
			return;
		}
		// The cursor proposes; the snap and then the constraints dispose. What the hand asked for is
		// kept for the ghost.
		DragProposalLocal.Add(DragPreviewLocal);
		DragPreviewLocal = CommitProposedLocation(DragVertexIndex, DragPreviewLocal, BasisPtr);

		// A drag moves geometry EVERY FRAME without a completed-operation notify (which would refresh
		// details panels per mouse move). Hosts that build their visuals still have to know.
		++ModelRevision;

		// The constraints naming this vertex directly take the proposal as their new parameter (an
		// Along slides along its span); the solve below then lands the vertex on it.
		Model->AbsorbProposal(Model->Vertices[DragVertexIndex].Id, DragPreviewLocal, BasisPtr);

		// A directly constrained vertex is PROJECTED while dragged, never pinned: the constraint is what
		// the user declared, the hand is only proposing. Any other held vertex is pinned, so a Length
		// endpoint follows the hand and its partner does the stretching.
		TArray<uint32> Pins;
		GatherDragPins({DragVertexIndex}, Pins);
		SolveConstraints(BasisPtr, Pins);
		const FPCGExClusterSketchVertex& V = Model->Vertices[DragVertexIndex];
		DragPreviewLocal = VertexLocation(V, BasisPtr);

		// Dropping onto another vertex MERGES into it (clusters cannot hold collocated vertices).
		// Layer-aware: under a rank-collapsed basis, prefer the stack member on the dragged vertex's
		// own hidden layer.
		MergeCandidateVertex = FindNearbyVertex(LocalRay, DragPreviewLocal, DragVertexIndex, BasisPtr, V.bLatticeBound ? &V.LatticeCoord : nullptr);
	}
	else if (DragMode == EDragMode::Connect)
	{
		const FPCGExClusterSketchModel* Model = GetReadModel();
		const FIntVector* SourceLayer = nullptr;
		if (Model && Model->Vertices.IsValidIndex(DragVertexIndex) && Model->Vertices[DragVertexIndex].bLatticeBound)
		{
			SourceLayer = &Model->Vertices[DragVertexIndex].LatticeCoord;
		}

		// Target acquisition, strictest first:
		// 1. the vertex the SNAPPED release point resolves onto (cell-exact -- an extrude can never
		//    stack a twin on an occupied node; layer-aware, so a projection stack yields the member on
		//    the SOURCE's hidden layer, not an arbitrary one);
		// 2. optionally, the vertex under the POINTER at a tightened radius (deliberate aim across the
		//    work plane, e.g. linking two distant vertices in 3D).
		DragTargetVertexIndex = FindNearbyVertex(LocalRay, DragPreviewLocal, DragVertexIndex, BasisPtr, SourceLayer);
		if (DragTargetVertexIndex == INDEX_NONE && bConnectToHover && Hover.IsVertex() && Hover.Index != DragVertexIndex)
		{
			if (Model && Model->Vertices.IsValidIndex(Hover.Index))
			{
				const FVector Pos = VertexLocation(Model->Vertices[Hover.Index], BasisPtr);
				const double T = FMath::Max(0.0, FVector::DotProduct(Pos - LocalRay.Origin, LocalRay.Direction));
				const double RayDist = FVector::Dist(LocalRay.Origin + LocalRay.Direction * T, Pos);
				if (RayDist <= PickRadiusAt(LocalRay, Pos, VertexPickFloor()) * PCGExSketchEditController::ConnectHoverTightFactor)
				{
					DragTargetVertexIndex = Hover.Index;
				}
			}
		}
	}
}

void FPCGExSketchEditController::EndDrag(const FRay& WorldRay)
{
	if (DragMode == EDragMode::None)
	{
		return;
	}

	UpdateDrag(WorldRay);

	if (DragMode == EDragMode::MoveEdge)
	{
		FPCGExClusterSketchModel* Model = Target->GetModel();
		if (Model && Model->Edges.IsValidIndex(DragEdgeIndex))
		{
			// Both ends are hand-placed now, and each may have landed on an edge or swept one across a vertex.
			const int32 A = Model->Edges[DragEdgeIndex].A;
			const int32 B = Model->Edges[DragEdgeIndex].B;
			const FPCGExLatticeBasis* BasisPtr = bDragHasBasis ? &DragBasis : nullptr;
			Model->MarkVertexAuthored(A);
			Model->MarkVertexAuthored(B);
			Model->EnforceSeparationAroundVertex(A, BasisPtr);
			Model->EnforceSeparationAroundVertex(B, BasisPtr);
			SolveConstraints(BasisPtr);
		}
		EndTransaction();
		NotifyModelChanged();
	}
	else if (DragMode == EDragMode::Move)
	{
		// Drop-on-vertex merges into it, still inside the drag's open transaction so the whole
		// move+merge is one undo step. Edges re-anchor onto the survivor; the absorbed vertex goes.
		FPCGExClusterSketchModel* Model = Target->GetModel();
		int32 FinalVertex = DragVertexIndex;
		if (MergeCandidateVertex != INDEX_NONE && Model)
		{
			const int32 Survivor = Model->MergeVertices(DragVertexIndex, MergeCandidateVertex);
			if (Survivor != INDEX_NONE)
			{
				FinalVertex = Survivor;
				ClearSelection();
				SelectVertex(Survivor);
			}
		}
		// Hand-placed now: dragging a tool-inserted vertex adopts it.
		if (Model)
		{
			Model->MarkVertexAuthored(FinalVertex);
			// Then re-enforce edge/vertex separation around the landing spot: a vertex dropped onto an
			// edge splits it, and merge-retargeted edges dissolve through the vertices they now cross
			// (collinear A-B-C never keeps A-C -- it splits and dedups into the existing chain).
			Model->EnforceSeparationAroundVertex(FinalVertex, bDragHasBasis ? &DragBasis : nullptr);
			SolveConstraints(bDragHasBasis ? &DragBasis : nullptr);
		}
		EndTransaction();
		NotifyModelChanged();
	}
	else // Connect
	{
		FPCGExClusterSketchModel* Model = Target->GetModel();
		const int32 Source = DragVertexIndex;
		const int32 ExistingTarget = DragTargetVertexIndex;
		const FVector PlacePoint = DragPreviewLocal;
		const bool bHasBasis = bDragHasBasis;
		const FPCGExLatticeBasis Basis = DragBasis;

		DragMode = EDragMode::None;
		DragVertexIndex = INDEX_NONE;
		DragTargetVertexIndex = INDEX_NONE;

		if (Model && Model->Vertices.IsValidIndex(Source))
		{
			int32 FarVertex = INDEX_NONE;
			if (ExistingTarget != INDEX_NONE && Model->Vertices.IsValidIndex(ExistingTarget))
			{
				const FScopedTransaction Transaction(LOCTEXT("ConnectVertices", "Connect Sketch Vertices"));
				Target->BeginAuthoring();
				Model->Connect(Source, ExistingTarget);
				// Deliberately wired by hand -- both ends are now authored.
				Model->MarkVertexAuthored(Source);
				Model->MarkVertexAuthored(ExistingTarget);
				// A deliberate long link across collinear vertices splits into the chain (A-C never
				// survives when B sits on it).
				Model->EnforceSeparationAroundVertex(ExistingTarget, bHasBasis ? &Basis : nullptr);
				SolveConstraints(bHasBasis ? &Basis : nullptr);
				FarVertex = ExistingTarget;
			}
			else
			{
				// The drafting gesture: release over nothing extrudes a new (snapped) vertex + edge.
				const FScopedTransaction Transaction(LOCTEXT("ExtrudeVertex", "Extrude Sketch Vertex"));
				Target->BeginAuthoring();

				// Resolved against the PRE-GESTURE model, BY VALUE: the adds below reallocate Vertices,
				// and the extruded edge would otherwise count toward the source's own degree.
				const UPCGExClusterSketchAuthoringSettings* Options = UPCGExClusterSketchAuthoringSettings::Get();
				const uint32 InheritedVertexData = Options->bExtrudeInheritsVertexData ? Model->Vertices[Source].DataId : PCGExSketch::InvalidRecordId;
				const uint32 InheritedEdgeData = Options->bExtrudeInheritsEdgeData ? Model->ResolveExtrudeEdgeDataId(Source) : PCGExSketch::InvalidRecordId;

				if (bSnapEnabled && bHasBasis)
				{
					// Inherit the source's unspanned components: extruding in a rank-collapsed basis
					// stays on the source's hidden layer instead of dropping the new vertex onto layer 0
					// (which is what wired edges across unrelated layers).
					const FPCGExClusterSketchVertex& SourceVertex = Model->Vertices[Source];
					const FIntVector Coord = SourceVertex.bLatticeBound
						? Basis.SnapWorldToCoordPreserving(PlacePoint, SourceVertex.LatticeCoord)
						: Basis.SnapWorldToCoord(PlacePoint);
					FarVertex = Model->AddLatticeVertex(Coord, Basis, InheritedVertexData);
				}
				else
				{
					FarVertex = Model->AddVertex(FTransform(PlacePoint), InheritedVertexData);
				}

				// Stamped before enforcement: a split then carries the record like any parent edge.
				bool bEdgeCreated = false;
				const int32 NewEdge = Model->Connect(Source, FarVertex, &bEdgeCreated);
				if (bEdgeCreated && InheritedEdgeData != PCGExSketch::InvalidRecordId)
				{
					Model->Edges[NewEdge].DataId = InheritedEdgeData;
				}

				Model->MarkVertexAuthored(Source); // extruding FROM a tool-inserted vertex adopts it
				Model->EnforceSeparationAroundVertex(FarVertex, bHasBasis ? &Basis : nullptr);
				SolveConstraints(bHasBasis ? &Basis : nullptr);
			}

			if (FarVertex != INDEX_NONE)
			{
				// Selecting the far end chains the gesture into a walk.
				ClearSelection();
				SelectVertex(FarVertex);
			}
			NotifyModelChanged();
		}
	}

	DragMode = EDragMode::None;
	DragVertexIndex = INDEX_NONE;
	DragEdgeIndex = INDEX_NONE;
	DragTargetVertexIndex = INDEX_NONE;
	MergeCandidateVertex = INDEX_NONE;
	DragProposalLocal.Reset();
	PlacementGesture = EPlacementGesture::None;
	Placement.ResetGuide();
}

void FPCGExSketchEditController::CancelDrag()
{
	bool bRolledBack = false;
	if (ActiveTransaction)
	{
		// A move already mutated under the open transaction -- cancelling rolls the object back.
		ActiveTransaction->Cancel();
		ActiveTransaction.Reset();
		bRolledBack = true;
	}
	DragMode = EDragMode::None;
	DragVertexIndex = INDEX_NONE;
	DragEdgeIndex = INDEX_NONE;
	DragTargetVertexIndex = INDEX_NONE;
	MergeCandidateVertex = INDEX_NONE;
	DragProposalLocal.Reset();
	PlacementGesture = EPlacementGesture::None;
	Placement.ResetGuide();

	// A rollback CHANGES GEOMETRY as surely as the drag did: crossings were computed against the
	// dragged shape, and a host whose visuals are built would otherwise keep showing where the vertex
	// never ended up.
	if (bRolledBack)
	{
		NotifyModelChanged();
	}
}

void FPCGExSketchEditController::DeleteSelection()
{
	ResolveSelectionIndices();

	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model || (!HasSelection()))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteSelection", "Delete Sketch Selection"));
	Target->BeginAuthoring();

	// Edges first (their indices die with vertex removal); both descending so indices stay valid.
	// BY INDEX: Disconnect resolves through FindEdge, which would remove the first edge sharing the
	// pair -- a different one whenever duplicates exist.
	TArray<int32> EdgeIndices = SelectedEdges.Array();
	EdgeIndices.Sort([](const int32 A, const int32 B)
	{
		return A > B;
	});
	for (const int32 e : EdgeIndices)
	{
		Model->RemoveEdgeAt(e);
	}

	TArray<int32> VertexIndices = SelectedVertices.Array();
	VertexIndices.Sort([](const int32 A, const int32 B)
	{
		return A > B;
	});
	for (const int32 v : VertexIndices)
	{
		Model->RemoveVertex(v);
	}

	// Tool residue never outlives the geometry that justified it.
	Model->RemoveOrphanSideEffectVertices();

	FPCGExLatticeBasis Basis;
	SolveConstraints(GetBasis(Basis) ? &Basis : nullptr);

	ClearSelection();
	ClearHover();
	NotifyModelChanged();
}

void FPCGExSketchEditController::SelectAll()
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		return;
	}
	ClearSelection();
	for (int32 i = 0; i < Model->Vertices.Num(); ++i)
	{
		SelectVertex(i);
	}
	for (int32 e = 0; e < Model->Edges.Num(); ++e)
	{
		SelectEdge(e);
	}
	NotifyModelChanged();
}

void FPCGExSketchEditController::ClearSelection()
{
	SelectedVertexIds.Reset();
	SelectedEdgeIds.Reset();
	SelectedVertices.Reset();
	SelectedEdges.Reset();
	LastSelectedVertex = INDEX_NONE;
	LastSelectedVertexId = PCGExSketch::InvalidElementId;
}

bool FPCGExSketchEditController::DeleteAtRay(const FRay& WorldRay)
{
	ResolveSelectionIndices();

	FPCGExClusterSketchModel* Model = Target->GetModel();
	const FPCGExSketchHit Hit = HitTest(WorldRay);
	if (!Model || !Hit.IsHit())
	{
		return false;
	}

	if (Hit.IsVertex())
	{
		if (!Model->Vertices.IsValidIndex(Hit.Index))
		{
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("DeleteVertex", "Delete Sketch Vertex"));
		Target->BeginAuthoring();
		Model->RemoveVertex(Hit.Index);
		Model->RemoveOrphanSideEffectVertices();
	}
	else
	{
		if (!Model->Edges.IsValidIndex(Hit.Index))
		{
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("DeleteEdge", "Delete Sketch Edge"));
		Target->BeginAuthoring();
		Model->RemoveEdgeAt(Hit.Index);
		Model->RemoveOrphanSideEffectVertices();
	}

	FPCGExLatticeBasis Basis;
	SolveConstraints(GetBasis(Basis) ? &Basis : nullptr);

	ClearSelection();
	ClearHover();
	NotifyModelChanged();
	return true;
}

int32 FPCGExSketchEditController::AddVertexAtRay(const FRay& WorldRay)
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return INDEX_NONE;
	}

	const FRay LocalRay = ToLocal(WorldRay);

	FPCGExLatticeBasis Basis;
	const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;

	// Resolved through the same call the hover preview uses, so the vertex lands exactly where the
	// preview showed it -- guide included.
	const FAddPlacement Placed = ResolveAddPlacement(LocalRay, BasisPtr);
	const FVector PlacePoint = Placed.Point;
	const FIntVector* AnchorLayer = Placed.bHasAnchorLayer ? &Placed.AnchorCoord : nullptr;

	// An occupied spot is reused, never twinned -- clusters cannot hold collocated vertices.
	const int32 Existing = FindNearbyVertex(LocalRay, PlacePoint, INDEX_NONE, BasisPtr, AnchorLayer);
	if (Existing != INDEX_NONE)
	{
		ClearSelection();
		SelectVertex(Existing);
		NotifyModelChanged();
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddVertex", "Add Sketch Vertex"));
	Target->BeginAuthoring();

	int32 NewVertex;
	if (bSnapEnabled && BasisPtr)
	{
		const FIntVector Coord = AnchorLayer
			? BasisPtr->SnapWorldToCoordPreserving(PlacePoint, Placed.AnchorCoord)
			: BasisPtr->SnapWorldToCoord(PlacePoint);
		NewVertex = Model->AddLatticeVertex(Coord, *BasisPtr);
	}
	else
	{
		NewVertex = Model->AddVertex(FTransform(PlacePoint));
	}

	// A vertex landing on an edge SPLITS it -- edges never pass through vertices.
	Model->EnforceSeparationAroundVertex(NewVertex, BasisPtr);
	SolveConstraints(BasisPtr);

	ClearSelection();
	SelectVertex(NewVertex);
	NotifyModelChanged();
	return NewVertex;
}

int32 FPCGExSketchEditController::FindNearbyVertex(const FRay& LocalRay, const FVector& LocalPoint, const int32 IgnoreVertex, const FPCGExLatticeBasis* Basis, const FIntVector* LayerRef) const
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		return INDEX_NONE;
	}

	// Pick radius capped below half a cell: on a dense lattice the screen-space radius could otherwise
	// reach a NEIGHBORING node and merge a drop that landed on a different cell.
	double RadiusCap = TNumericLimits<double>::Max();
	if (Basis && Basis->NumAxes > 0)
	{
		double SmallestAxis = TNumericLimits<double>::Max();
		for (int32 k = 0; k < Basis->NumAxes; ++k)
		{
			SmallestAxis = FMath::Min(SmallestAxis, Basis->AxisVecs[k].Size());
		}
		RadiusCap = SmallestAxis * 0.45;
	}

	const int32 NumAxes = Basis ? Basis->NumAxes : 3;
	auto SharesLayer = [&](const FPCGExClusterSketchVertex& V) -> bool
	{
		if (!LayerRef || !V.bLatticeBound)
		{
			return false;
		}
		if (NumAxes <= 2 && V.LatticeCoord.Z != LayerRef->Z)
		{
			return false;
		}
		if (NumAxes <= 1 && V.LatticeCoord.Y != LayerRef->Y)
		{
			return false;
		}
		return true;
	};

	int32 Best = INDEX_NONE;
	double BestDist = TNumericLimits<double>::Max();
	int32 BestSameLayer = INDEX_NONE;
	double BestSameLayerDist = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Model->Vertices.Num(); ++i)
	{
		if (i == IgnoreVertex)
		{
			continue;
		}
		const FPCGExClusterSketchVertex& V = Model->Vertices[i];
		const FVector Pos = VertexLocation(V, Basis);
		const double Dist = FVector::Dist(Pos, LocalPoint);
		if (Dist > FMath::Min(PickRadiusAt(LocalRay, Pos, VertexPickFloor()), RadiusCap))
		{
			continue;
		}
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = i;
		}
		if (SharesLayer(V) && Dist < BestSameLayerDist)
		{
			BestSameLayerDist = Dist;
			BestSameLayer = i;
		}
	}
	// The gesture source's own layer wins over a merely-nearest stack member.
	return BestSameLayer != INDEX_NONE ? BestSameLayer : Best;
}

void FPCGExSketchEditController::EnsurePlacementGesture(const EPlacementGesture InGesture, const int32 InAnchorVertex, const FVector& InAnchor, const FPCGExLatticeBasis* Basis, const bool bPointWillSnap)
{
	if (PlacementGesture != InGesture || PlacementAnchorVertex != InAnchorVertex ||
		!InAnchor.Equals(PlacementAnchor, PCGExSketchEditController::AnchorDriftTolerance))
	{
		PlacementGesture = InGesture;
		PlacementAnchorVertex = InAnchorVertex;
		PlacementAnchor = InAnchor;
		Placement.BeginGesture(InAnchor, Basis);
	}

	// Rebuilt every frame so a guide option or the snap toggle takes effect mid-gesture; the latch keys
	// on guide identity rather than array position, so this never drops what was captured.
	Placement.BuildCandidates(GetReadModel(), InAnchorVertex, Basis, bPointWillSnap);
}

FPCGExSketchEditController::FAddPlacement FPCGExSketchEditController::ResolveAddPlacement(const FRay& LocalRay, const FPCGExLatticeBasis* Basis)
{
	FAddPlacement Result;

	// Anchor: the vertex selected LAST if it is still valid, else the lattice origin. A bound anchor
	// also donates its hidden layer, so adding in a rank-collapsed basis stays on it.
	FVector Anchor = Basis ? Basis->Origin : FVector::ZeroVector;
	int32 AnchorVertex = INDEX_NONE;
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (Model && SelectedVertices.Contains(LastSelectedVertex) && Model->Vertices.IsValidIndex(LastSelectedVertex))
	{
		const FPCGExClusterSketchVertex& AnchorVtx = Model->Vertices[LastSelectedVertex];
		Anchor = VertexLocation(AnchorVtx, Basis);
		AnchorVertex = LastSelectedVertex;
		if (AnchorVtx.bLatticeBound)
		{
			Result.AnchorCoord = AnchorVtx.LatticeCoord;
			Result.bHasAnchorLayer = true;
		}
	}

	EnsurePlacementGesture(EPlacementGesture::Add, AnchorVertex, Anchor, Basis, bSnapEnabled && Basis != nullptr);

	Result.bResolved = Placement.Resolve(LocalRay, Result.Point);
	if (!Result.bResolved)
	{
		// A held guide seen end-on cannot answer. Freeze on the last previewed point, exactly as a drag
		// freezes -- teleporting down the ray would place a vertex nowhere near the ghost that was
		// showing. Only a gesture that has never previewed has nothing better to fall back to.
		Result.Point = bHasAddPreview
			? AddPreviewLocal
			: LocalRay.Origin + LocalRay.Direction * PCGExSketchPlacement::FallbackPlaceDistance;
	}

	if (bSnapEnabled && Basis)
	{
		Result.Point = Basis->CoordToWorld(Basis->SnapWorldToCoord(Result.Point));
	}
	return Result;
}

void FPCGExSketchEditController::SetAddIntent(const bool bIntent)
{
	if (bAddIntent == bIntent)
	{
		return;
	}
	bAddIntent = bIntent;
	if (!bAddIntent)
	{
		bHasAddPreview = false;
	}
	RefreshPlacement();
}

bool FPCGExSketchEditController::CyclePlacementGuide()
{
	// Gated on a LIVE preview, not merely on a solver that still remembers a gesture: the key has other
	// meanings everywhere else and must fall through untouched.
	if (!HasPlacementPreview())
	{
		return false;
	}
	Placement.CycleGuide();
	RefreshPlacement();
	return true;
}

bool FPCGExSketchEditController::ReleasePlacementGuide()
{
	if (!HasPlacementPreview() || !Placement.ReleaseGuide())
	{
		return false;
	}
	RefreshPlacement();
	return true;
}

void FPCGExSketchEditController::RefreshPlacement()
{
	if (!bHasLastLocalRay)
	{
		return;
	}

	if (DragMode != EDragMode::None)
	{
		ApplyDrag(LastLocalRay);
		return;
	}

	if (bAddIntent && !Hover.IsHit())
	{
		FPCGExLatticeBasis Basis;
		const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;
		AddPreviewLocal = ResolveAddPlacement(LastLocalRay, BasisPtr).Point;
		bHasAddPreview = true;
	}
	else
	{
		bHasAddPreview = false;
	}
}

void FPCGExSketchEditController::EndTransaction()
{
	ActiveTransaction.Reset();
}


void FPCGExSketchEditController::NotifyModelChanged()
{
	++ModelRevision;
	// Structural repair (splits, merges, orphan sweeps) may have shifted whatever the operation selected.
	ResolveSelectionIndices();
	RefreshCrossings();
	RefreshConstraintResiduals();
	{
		TGuardValue<bool> NotifyGuard(bNotifying, true);
		Target->NotifyChanged();
	}
	OnChanged.Broadcast();
}

void FPCGExSketchEditController::NotifyInteractiveChange()
{
	// The revision alone repaints: both hosts push it into the mesh layer every frame via DrawWithComponent.
	++ModelRevision;
	RefreshConstraintResiduals();
}

void FPCGExSketchEditController::NotifyExternalChange()
{
	++ModelRevision;
	ResolveSelectionIndices();
	// Counts may match across an undo that only moved geometry, so the fingerprint gate is bypassed.
	CrossingsRevision = INDEX_NONE;
	RefreshCrossings();
	RefreshConstraintResiduals();
	OnChanged.Broadcast();
}

void FPCGExSketchEditController::GatherDragPins(const TConstArrayView<int32> InHeldVertices, TArray<uint32>& OutPinnedIds) const
{
	OutPinnedIds.Reset();
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		return;
	}
	for (const int32 Index : InHeldVertices)
	{
		if (Model->Vertices.IsValidIndex(Index) && !Model->IsVertexDirectSubject(Model->Vertices[Index].Id))
		{
			OutPinnedIds.Add(Model->Vertices[Index].Id);
		}
	}
}

void FPCGExSketchEditController::SolveConstraints(const FPCGExLatticeBasis* Basis, const TConstArrayView<uint32> InPinnedIds)
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return;
	}
	TArray<FPCGExSketchConstraintResidual> Residuals;
	if (Model->SolveConstraints(Basis, InPinnedIds, &Residuals))
	{
		++ModelRevision;
	}
	ConstraintResiduals.Reset();
	for (const FPCGExSketchConstraintResidual& R : Residuals)
	{
		ConstraintResiduals.Add(R.ConstraintId, R);
	}
}

void FPCGExSketchEditController::RefreshConstraintResiduals()
{
	ConstraintResiduals.Reset();
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		return;
	}
	FPCGExLatticeBasis Basis;
	TArray<FPCGExSketchConstraintResidual> Residuals;
	Model->EvaluateConstraints(GetBasis(Basis) ? &Basis : nullptr, Residuals);
	for (const FPCGExSketchConstraintResidual& R : Residuals)
	{
		ConstraintResiduals.Add(R.ConstraintId, R);
	}
}

FVector FPCGExSketchEditController::CommitProposedLocation(const int32 VertexIndex, const FVector& Proposed, const FPCGExLatticeBasis* Basis)
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model || !Model->Vertices.IsValidIndex(VertexIndex))
	{
		return Proposed;
	}
	FPCGExClusterSketchVertex& V = Model->Vertices[VertexIndex];
	if (V.bLatticeBound && Basis)
	{
		// Coords stay authoritative for bound vertices: snap regardless of the toggle, so a bound
		// vertex can never be dragged off-lattice while a basis exists. Preserving: dragging in a
		// rank-collapsed basis edits the spanned components only, the stash survives.
		V.LatticeCoord = Basis->SnapWorldToCoordPreserving(Proposed, V.LatticeCoord);
		V.Transform.SetLocation(Basis->CoordToWorld(V.LatticeCoord));
	}
	else if (bSnapEnabled && Basis)
	{
		V.Transform.SetLocation(Basis->CoordToWorld(Basis->SnapWorldToCoord(Proposed)));
	}
	else
	{
		V.Transform.SetLocation(Proposed);
	}
	return V.Transform.GetLocation();
}

bool FPCGExSketchEditController::IsDragProposalDiverging() const
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model || DragProposalLocal.IsEmpty())
	{
		return false;
	}
	FPCGExLatticeBasis Basis;
	const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;
	constexpr double TolSq = 1.0; // below a unit the tether would only be noise
	if (DragMode == EDragMode::Move && Model->Vertices.IsValidIndex(DragVertexIndex))
	{
		return FVector::DistSquared(DragProposalLocal[0], VertexLocation(Model->Vertices[DragVertexIndex], BasisPtr)) > TolSq;
	}
	if (DragMode == EDragMode::MoveEdge && Model->Edges.IsValidIndex(DragEdgeIndex) && DragProposalLocal.Num() == 2)
	{
		const FPCGExClusterSketchEdge& E = Model->Edges[DragEdgeIndex];
		if (!Model->Vertices.IsValidIndex(E.A) || !Model->Vertices.IsValidIndex(E.B))
		{
			return false;
		}
		return FVector::DistSquared(DragProposalLocal[0], VertexLocation(Model->Vertices[E.A], BasisPtr)) > TolSq
			|| FVector::DistSquared(DragProposalLocal[1], VertexLocation(Model->Vertices[E.B], BasisPtr)) > TolSq;
	}
	return false;
}

void FPCGExSketchEditController::GatherSelectedIds(TArray<uint32>& OutVertexIds, TArray<uint32>& OutEdgeIds) const
{
	OutVertexIds.Reset();
	OutEdgeIds.Reset();
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model)
	{
		return;
	}
	for (const int32 i : SelectedVertices)
	{
		if (Model->Vertices.IsValidIndex(i))
		{
			OutVertexIds.Add(Model->Vertices[i].Id);
		}
	}
	for (const int32 e : SelectedEdges)
	{
		if (Model->Edges.IsValidIndex(e))
		{
			OutEdgeIds.Add(Model->Edges[e].Id);
		}
	}
}

bool FPCGExSketchEditController::CanAddConstraint(const UScriptStruct* InType) const
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	if (!Model || !InType || !InType->IsChildOf(FPCGExSketchConstraint::StaticStruct()) || !Target->CanEdit())
	{
		return false;
	}
	TArray<uint32> VertexIds;
	TArray<uint32> EdgeIds;
	GatherSelectedIds(VertexIds, EdgeIds);

	// A scratch instance answers; nothing is attached.
	FInstancedStruct Scratch;
	Scratch.InitializeAs(InType);
	return Scratch.GetMutable<FPCGExSketchConstraint>().BuildSubjectsFromSelection(*Model, VertexIds, EdgeIds);
}

uint32 FPCGExSketchEditController::AddConstraintToSelection(const UScriptStruct* InType)
{
	ResolveSelectionIndices();
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model || !InType || !InType->IsChildOf(FPCGExSketchConstraint::StaticStruct()))
	{
		return PCGExSketch::InvalidElementId;
	}
	TArray<uint32> VertexIds;
	TArray<uint32> EdgeIds;
	GatherSelectedIds(VertexIds, EdgeIds);

	FInstancedStruct Entry;
	Entry.InitializeAs(InType);
	if (!Entry.GetMutable<FPCGExSketchConstraint>().BuildSubjectsFromSelection(*Model, VertexIds, EdgeIds))
	{
		return PCGExSketch::InvalidElementId;
	}

	FPCGExLatticeBasis Basis;
	const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;

	const FScopedTransaction Transaction(LOCTEXT("AddConstraint", "Add Sketch Constraint"));
	Target->BeginAuthoring();
	const uint32 Id = Model->AddConstraint(MoveTemp(Entry), BasisPtr);
	SolveConstraints(BasisPtr);
	NotifyModelChanged();
	return Id;
}

bool FPCGExSketchEditController::RemoveConstraint(const uint32 InConstraintId)
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model || !Model->FindConstraint(InConstraintId))
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("RemoveConstraint", "Remove Sketch Constraint"));
	Target->BeginAuthoring();
	Model->RemoveConstraint(InConstraintId);
	FPCGExLatticeBasis Basis;
	SolveConstraints(GetBasis(Basis) ? &Basis : nullptr);
	NotifyModelChanged();
	return true;
}

int32 FPCGExSketchEditController::ClearConstraintsOnSelection()
{
	ResolveSelectionIndices();
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model || !HasSelection())
	{
		return 0;
	}

	TArray<uint32> Ids;
	for (const int32 i : SelectedVertices)
	{
		Ids.Add(Model->Vertices[i].Id);
	}
	for (const int32 e : SelectedEdges)
	{
		Ids.Add(Model->Edges[e].Id);
	}

	FScopedTransaction Transaction(LOCTEXT("ClearConstraints", "Clear Sketch Constraints"));
	Target->BeginAuthoring();

	int32 NumRemoved = 0;
	for (const uint32 Id : Ids)
	{
		NumRemoved += Model->RemoveConstraintsOf(Id);
	}
	if (NumRemoved == 0)
	{
		// Nothing changed: Modify() already dirtied the host, so the transaction must not stand.
		Transaction.Cancel();
		return 0;
	}

	FPCGExLatticeBasis Basis;
	SolveConstraints(GetBasis(Basis) ? &Basis : nullptr);
	NotifyModelChanged();
	return NumRemoved;
}

bool FPCGExSketchEditController::SetConstraintEnabled(const uint32 InConstraintId, const bool bEnabled)
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	FPCGExSketchConstraint* Constraint = Model ? Model->FindConstraintMutable(InConstraintId) : nullptr;
	if (!Constraint)
	{
		return false;
	}
	if (Constraint->bEnabled == bEnabled)
	{
		return true;
	}

	const FScopedTransaction Transaction(bEnabled ? LOCTEXT("EnableConstraint", "Enable Sketch Constraint") : LOCTEXT("DisableConstraint", "Disable Sketch Constraint"));
	Target->BeginAuthoring();
	Constraint->bEnabled = bEnabled;

	FPCGExLatticeBasis Basis;
	SolveConstraints(GetBasis(Basis) ? &Basis : nullptr);
	NotifyModelChanged();
	return true;
}

int32 FPCGExSketchEditController::MergeCollocatedVertices()
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return 0;
	}

	FPCGExLatticeBasis Basis;
	const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;

	const FScopedTransaction Transaction(LOCTEXT("MergeCollocated", "Merge Collocated Sketch Vertices"));
	UObject* TransactionObject = Target->GetTransactionObject();
	Target->BeginAuthoring();

	const int32 NumMerged = Model->MergeCollocatedVertices(BasisPtr);

	ClearSelection();
	ClearHover();
	{
		FPCGExLatticeBasis SolveBasis;
		SolveConstraints(GetBasis(SolveBasis) ? &SolveBasis : nullptr);
	}
	PCGExEditor::NotifyObjectChanged(TransactionObject);
	NotifyModelChanged();
	return NumMerged;
}

int32 FPCGExSketchEditController::RemoveInvalidEdges()
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return 0;
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveInvalidEdges", "Remove Invalid Sketch Edges"));
	UObject* TransactionObject = Target->GetTransactionObject();
	Target->BeginAuthoring();

	const int32 NumRemoved = Model->RemoveInvalidEdges();

	ClearSelection();
	ClearHover();
	{
		FPCGExLatticeBasis SolveBasis;
		SolveConstraints(GetBasis(SolveBasis) ? &SolveBasis : nullptr);
	}
	PCGExEditor::NotifyObjectChanged(TransactionObject);
	NotifyModelChanged();
	return NumRemoved;
}

int32 FPCGExSketchEditController::SplitOverlappingEdges()
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return 0;
	}

	FPCGExLatticeBasis Basis;
	const FPCGExLatticeBasis* BasisPtr = GetBasis(Basis) ? &Basis : nullptr;

	const FScopedTransaction Transaction(LOCTEXT("SplitOverlappingEdges", "Split Overlapping Sketch Edges"));
	UObject* TransactionObject = Target->GetTransactionObject();
	Target->BeginAuthoring();

	const int32 NumSplits = Model->SplitOverlappingEdges(BasisPtr);

	ClearSelection();
	ClearHover();
	{
		FPCGExLatticeBasis SolveBasis;
		SolveConstraints(GetBasis(SolveBasis) ? &SolveBasis : nullptr);
	}
	PCGExEditor::NotifyObjectChanged(TransactionObject);
	NotifyModelChanged();
	return NumSplits;
}

int32 FPCGExSketchEditController::PurgeUnusedDataRecords()
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	if (!Model)
	{
		return 0;
	}

	const FScopedTransaction Transaction(LOCTEXT("PurgeDataRecords", "Purge Unused Sketch Data Records"));
	UObject* TransactionObject = Target->GetTransactionObject();
	Target->BeginAuthoring();

	const int32 NumPurged = Model->PurgeUnreferencedRecords();
	{
		FPCGExLatticeBasis SolveBasis;
		SolveConstraints(GetBasis(SolveBasis) ? &SolveBasis : nullptr);
	}
	PCGExEditor::NotifyObjectChanged(TransactionObject);
	NotifyModelChanged();
	return NumPurged;
}

void FPCGExSketchEditController::RefreshCrossings()
{
	const FPCGExClusterSketchModel* Model = GetReadModel();
	const int32 VertexCount = Model ? Model->Vertices.Num() : 0;
	const int32 EdgeCount = Model ? Model->Edges.Num() : 0;
	if (CrossingsRevision == ModelRevision && CrossingsVertexCount == VertexCount && CrossingsEdgeCount == EdgeCount)
	{
		return;
	}
	CrossingsRevision = ModelRevision;
	CrossingsVertexCount = VertexCount;
	CrossingsEdgeCount = EdgeCount;

	if (!Model)
	{
		Crossings.Reset();
		return;
	}
	FPCGExLatticeBasis Basis;
	Model->FindEdgeCrossings(Crossings, GetBasis(Basis) ? &Basis : nullptr);
}

bool FPCGExSketchEditController::MaterializeCrossingAtRay(const FRay& WorldRay)
{
	FPCGExClusterSketchModel* Model = Target->GetModel();
	const FPCGExSketchHit Hit = HitTest(WorldRay);
	if (!Model || !Hit.IsCrossing() || !Crossings.IsValidIndex(Hit.Index))
	{
		return false;
	}

	const FPCGExClusterSketchCrossing Crossing = Crossings[Hit.Index];

	FScopedTransaction Transaction(LOCTEXT("MaterializeCrossing", "Materialize Sketch Crossing"));
	Target->BeginAuthoring();

	FPCGExLatticeBasis Basis;
	const int32 NewVertex = Model->MaterializeCrossing(Crossing.EdgeA, Crossing.EdgeB, Crossing.Location, GetBasis(Basis) ? &Basis : nullptr);
	if (NewVertex == INDEX_NONE)
	{
		// The crossing no longer holds (geometry moved under the cached ghost). Modify() has already
		// snapshotted and dirtied the target, so the transaction must be cancelled, not committed.
		Transaction.Cancel();
		// Stale ghosts: the rollback leaves the shape fingerprint untouched, so force the re-derive.
		CrossingsRevision = INDEX_NONE;
		RefreshCrossings();
		return false;
	}

	SolveConstraints(GetBasis(Basis) ? &Basis : nullptr);
	ClearSelection();
	SelectVertex(NewVertex);
	ClearHover();
	NotifyModelChanged();
	return true;
}
#pragma endregion

#undef LOCTEXT_NAMESPACE
