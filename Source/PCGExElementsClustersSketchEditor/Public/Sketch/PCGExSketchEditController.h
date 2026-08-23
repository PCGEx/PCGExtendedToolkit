// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Lattice/PCGExLatticeBasis.h"
#include "Sketch/PCGExClusterSketchConstraint.h"
#include "Sketch/PCGExClusterSketchModel.h"
#include "Sketch/PCGExSketchPlacement.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FScopedTransaction;
class FTransactionObjectEvent;
class UPCGExClusterSketch;
class UPCGExClusterSnapProvider;
struct FPropertyChangedEvent;

/**
 * What the sketch edit controller edits. One implementation per authoring host: the asset (standalone
 * editor, below), a component or a cage later. The controller performs EVERY mutation through the
 * model's mutation API against this seam, so hosts share selection/gesture/undo logic wholesale.
 */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API IPCGExSketchEditTarget
{
public:
	virtual ~IPCGExSketchEditTarget() = default;

	virtual FPCGExClusterSketchModel* GetModel() = 0;
	virtual const FPCGExClusterSketchModel* GetModel() const = 0;
	virtual const UPCGExClusterSnapProvider* GetSnapProvider() const = 0;

	/** Whether authoring is allowed AT ALL. Distinct from a null GetModel(), which conflates "read-only"
	 *  (a component instancing an asset -- alive, inspectable, never authored through) with "host died". */
	virtual bool CanEdit() const = 0;

	/** Why CanEdit() is false, for the panel's banner. Only reached when it IS false. */
	virtual FText GetReadOnlyReason() const;

	/** The object Modify() is called on inside every transaction (the asset / the component). */
	virtual UObject* GetTransactionObject() = 0;

	/**
	 * Whether a transaction or property event on InObject concerns THIS sketch. The set is wider than the
	 * transaction object: a construction-script component is never transacted itself (Modify redirects
	 * to its actor), and the model lives in a payload subobject. Consumers watching the engine's
	 * object-level delegates must filter through this, never by pointer equality with one host object.
	 */
	virtual bool OwnsObject(const UObject* InObject) const = 0;

	/**
	 * The object the panel's details view is rooted at. Not the transaction object: a component keeps its
	 * authored tier in a payload subobject, so rooting there gives both hosts the same shape -- Model,
	 * SnapProvider, Decorators as top-level rows. Null means there is nothing authored to show.
	 */
	virtual UObject* GetDetailsObject()
	{
		return GetTransactionObject();
	}

	/**
	 * Open an authoring edit. Transacts the host AND the authored-tier subobject (which is a separate
	 * UObject, so a host-only Modify leaves record edits outside the transaction), and lets a host
	 * record that its content is now its own.
	 *
	 * Use this at EVERY mutation site instead of GetTransactionObject()->Modify().
	 */
	virtual void BeginAuthoring();

	/** Model space -> world. Identity for the asset editor; a component host returns its transform. */
	virtual FTransform GetLocalToWorld() const = 0;

	/** Fired after every completed operation so the host can refresh (viewport invalidate, details). */
	virtual void NotifyChanged() = 0;
};

/** The standalone editor's target: edits a UPCGExClusterSketch asset in place, identity transform. */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchAssetEditTarget final : public IPCGExSketchEditTarget
{
public:
	explicit FPCGExSketchAssetEditTarget(UPCGExClusterSketch* InSketch);

	virtual FPCGExClusterSketchModel* GetModel() override;
	virtual const FPCGExClusterSketchModel* GetModel() const override;
	virtual const UPCGExClusterSnapProvider* GetSnapProvider() const override;
	virtual bool CanEdit() const override;
	virtual UObject* GetTransactionObject() override;
	virtual bool OwnsObject(const UObject* InObject) const override;

	virtual FTransform GetLocalToWorld() const override
	{
		return FTransform::Identity;
	}

	virtual void NotifyChanged() override;

private:
	TWeakObjectPtr<UPCGExClusterSketch> Sketch;
};

/** What a ray hit in the sketch. */
struct PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchHit
{
	enum class EType : uint8
	{
		None,
		Vertex,
		Edge,
		Crossing
	};

	EType Type = EType::None;
	int32 Index = INDEX_NONE;
	/** Ray parameter of the hit (world units along the ray) -- the ITF hit depth. */
	double RayT = 0.0;

	bool IsHit() const
	{
		return Type != EType::None;
	}

	bool IsVertex() const
	{
		return Type == EType::Vertex;
	}

	bool IsCrossing() const
	{
		return Type == EType::Crossing;
	}

	bool IsEdge() const
	{
		return Type == EType::Edge;
	}
};

/**
 * Host-agnostic sketch authoring: selection, hover, click/drag gestures, add/move/connect/disconnect/
 * delete, snapping -- everything except input plumbing and drawing. Hosts feed it WORLD rays (from ITF
 * behaviors or anything else) and render its state via FPCGExSketchDrawHelper; every mutation is one
 * scoped transaction on the target's transaction object.
 *
 * Gestures (the host maps modifiers to the two flags):
 *  - Click: select (bAdditive toggles); click on nothing clears; bAddOnEmpty + nothing = add a vertex.
 *  - Drag from a vertex: move it (snapped when a basis is active).
 *  - Connect-drag from a vertex (bConnect): release on a vertex links them; release on nothing adds a
 *    snapped vertex there AND links it (the drafting gesture); the far vertex becomes the selection.
 */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExSketchEditController
{
public:
	enum class EDragMode : uint8
	{
		None,
		Move,
		Connect,
		/** Both endpoints translate by the cursor delta -- the edge itself never moves, its vertices do. */
		MoveEdge
	};

	explicit FPCGExSketchEditController(const TSharedRef<IPCGExSketchEditTarget>& InTarget);
	~FPCGExSketchEditController();

	//~ Queries (all rays in WORLD space)
	FPCGExSketchHit HitTest(const FRay& WorldRay) const;

	//~ Hover
	void UpdateHover(const FRay& WorldRay);
	void ClearHover();

	//~ Click
	void HandleClick(const FRay& WorldRay, bool bAdditive, bool bAddOnEmpty);

	//~ Drag. A press on a vertex moves it (or connects from it); a press on an edge moves both endpoints.
	void BeginDrag(const FRay& WorldRay, bool bConnect);
	void UpdateDrag(const FRay& WorldRay);
	void EndDrag(const FRay& WorldRay);
	void CancelDrag();

	//~ Operations
	void DeleteSelection();
	void SelectAll();
	void ClearSelection();
	int32 AddVertexAtRay(const FRay& WorldRay);
	/** Materialize the ghost crossing under the ray: insert its vertex and split both edges through it.
	 *  @return true if one was materialized. */
	bool MaterializeCrossingAtRay(const FRay& WorldRay);

	//~ Authored-data cleanup, and the ONLY entry point for it. Each is one transaction on the target, so
	//~ a component's inline sketch gets them exactly as an asset does.
	/** Merge every vertex resolving onto an already-occupied printed location. @return merges performed. */
	int32 MergeCollocatedVertices();
	/** Drop out-of-range, self-loop and duplicate edges. @return edges removed. */
	int32 RemoveInvalidEdges();
	/** Split edges through vertices and materialize every crossing. @return splits + crossings. */
	int32 SplitOverlappingEdges();
	/** Drop records no item references, both layers. @return records removed. */
	int32 PurgeUnusedDataRecords();

	//~ Constraints. Each is one transaction; the solve runs inside it.
	/** Selected element ids, for the type pickers. */
	void GatherSelectedIds(TArray<uint32>& OutVertexIds, TArray<uint32>& OutEdgeIds) const;
	/** Whether InType would attach to the current selection -- what a picker offers. */
	bool CanAddConstraint(const UScriptStruct* InType) const;
	/** Attach a constraint of InType to the selection (subjects built by the type, parameters seeded from
	 *  the geometry). @return the constraint id, or 0 when the selection does not fit. */
	uint32 AddConstraintToSelection(const UScriptStruct* InType);
	/** Remove one constraint. @return false when the id is unknown. */
	bool RemoveConstraint(uint32 InConstraintId);
	/** Remove every constraint naming a selected element. @return the number removed. */
	int32 ClearConstraintsOnSelection();
	/** Toggle one constraint. @return false when the id is unknown. */
	bool SetConstraintEnabled(uint32 InConstraintId, bool bEnabled);

	/** Residuals from the last solve or evaluation, keyed by constraint id. Refreshed with the crossings. */
	const TMap<uint32, FPCGExSketchConstraintResidual>& GetConstraintResiduals() const
	{
		return ConstraintResiduals;
	}

	/** Recompute the ghost crossings. Cheap at sketch scale; called after every mutation. */
	void RefreshCrossings();

	/** Refresh ghosts, then tell the host. Every mutation ends here -- including one a panel made
	 *  directly on the model (record authoring), which is why this is public. */
	void NotifyModelChanged();

	/**
	 * The model changed UNDER the controller -- undo, a details-panel edit, a provider swap. Bumps the
	 * revision (undo cannot rewind it), drops stale indices, re-derives ghosts and broadcasts OnChanged,
	 * but does NOT notify the host: it already knows. Wired to the engine's transaction and
	 * property-change delegates through IPCGExSketchEditTarget::OwnsObject.
	 */
	void NotifyExternalChange();

	/** A panel slider is moving the model every tick: bump the revision (mesh layer + crossings follow)
	 *  and refresh residuals, with no host notify -- the commit does that once, on release. */
	void NotifyInteractiveChange();

	/** Fired by NotifyModelChanged, so it carries SELECTION changes as well as model mutations. */
	FSimpleMulticastDelegate OnChanged;

	/** Hypothetical crossings offered as ghosts -- never cut automatically. */
	const TArray<FPCGExClusterSketchCrossing>& GetCrossings() const
	{
		return Crossings;
	}

	/** Bumped by every geometry mutation, INCLUDING the live per-frame ones a drag makes (which
	 *  deliberately skip NotifyChanged -- that is the completed-operation notify, far too heavy to fire
	 *  per mouse move). A host whose visuals are built rather than drawn per frame watches this to know
	 *  its geometry went stale. */
	int32 GetModelRevision() const
	{
		return ModelRevision;
	}

	/** Delete the element under the ray -- vertex (with its edges) or edge, whichever the hit-test
	 *  yields (the Alt+click gesture). Side-effect vertices orphaned by the removal go with it.
	 *  @return true if something was removed. */
	bool DeleteAtRay(const FRay& WorldRay);

	//~ Snapping
	bool IsSnapEnabled() const
	{
		return bSnapEnabled;
	}

	void SetSnapEnabled(const bool bEnabled)
	{
		bSnapEnabled = bEnabled;
	}

	//~ Gesture options
	/** When true, a connect drag may also latch the vertex under the POINTER (tight radius) as its
	 *  target; the snapped-release-point resolution is always on (the collocation guarantee). */
	bool IsConnectToHoverEnabled() const
	{
		return bConnectToHover;
	}

	void SetConnectToHoverEnabled(const bool bEnabled)
	{
		bConnectToHover = bEnabled;
	}

	/** Host sets this while its delete modifier is held; the hovered element draws as a delete target. */
	void SetDeleteIntent(const bool bIntent)
	{
		bDeleteIntent = bIntent;
	}

	bool GetDeleteIntent() const
	{
		return bDeleteIntent;
	}

	/** Host sets this while its ADD modifier is held. Hovering then previews where a vertex would land,
	 *  guide included -- an inferred guide the user cannot see before committing is one they cannot
	 *  steer. */
	void SetAddIntent(bool bIntent);

	bool GetAddIntent() const
	{
		return bAddIntent;
	}

	//~ Placement guides
	/** Step the active guide to the next candidate; past the last one, back to inference. Re-resolves
	 *  against the last cursor ray, since the key that triggers this carries none. @return true when a
	 *  gesture was live to steer. */
	bool CyclePlacementGuide();
	/** Drop the active guide until the next cycle. @return true when there was one to drop. */
	bool ReleasePlacementGuide();
	/** Re-run the live gesture against the last cursor ray. For modifier and key changes, which reach a
	 *  drag WITHOUT a fresh ray -- the ITF hands a capture the keyboard state, whose mouse data is
	 *  invalid, so no drag update follows on its own. */
	void RefreshPlacement();

	const FPCGExSketchPlacementSolver& GetPlacement() const
	{
		return Placement;
	}

	/** True while a gesture (drag or add-intent hover) has a live placement point to draw. */
	bool HasPlacementPreview() const
	{
		return DragMode != EDragMode::None || bHasAddPreview;
	}

	/** Where the live gesture currently points, in MODEL space. A drag outranks the add ghost, so
	 *  consumers never have to rank the two themselves and cannot disagree about which is live. */
	const FVector& GetPlacementPoint() const
	{
		return DragMode != EDragMode::None ? DragPreviewLocal : AddPreviewLocal;
	}

	/**
	 * Where the cursor PROPOSED the dragged element before the constraints had their say, in MODEL space
	 * -- drawn as a ghost tethered to where it actually landed, so the hand sees it was heard. Empty
	 * when nothing is dragged; one point for a vertex, two for an edge.
	 */
	TConstArrayView<FVector> GetDragProposal() const
	{
		return DragProposalLocal;
	}

	/** True while the proposal and the solved result differ: the tether is worth drawing. */
	bool IsDragProposalDiverging() const;

	/** Edge being dragged in MoveEdge, else INDEX_NONE. */
	int32 GetDragEdge() const
	{
		return DragEdgeIndex;
	}

	/** Basis from the target's provider; false when there is none. Rebuilt on demand -- never cached. */
	bool GetBasis(FPCGExLatticeBasis& OutBasis) const;

	//~ Draw-state accessors (consumed by FPCGExSketchDrawHelper; indices may be stale after external
	//~ edits -- consumers must IsValidIndex-guard, the controller sanitizes on its own operations)
	const IPCGExSketchEditTarget& GetTarget() const
	{
		return Target.Get();
	}

	/** Authoring seam for panels that write the model directly (record assignment): they need the
	 *  transaction object and the mutable model, then end on NotifyModelChanged like everything else. */
	IPCGExSketchEditTarget& GetTarget()
	{
		return Target.Get();
	}

	const TSet<int32>& GetSelectedVertices() const
	{
		return SelectedVertices;
	}

	const TSet<int32>& GetSelectedEdges() const
	{
		return SelectedEdges;
	}

	const FPCGExSketchHit& GetHover() const
	{
		return Hover;
	}

	EDragMode GetDragMode() const
	{
		return DragMode;
	}

	int32 GetDragVertex() const
	{
		return DragVertexIndex;
	}

	int32 GetDragTargetVertex() const
	{
		return DragTargetVertexIndex;
	}

	/** Current drag point in MODEL space (snap already applied) -- the move ghost / connect line end. */
	const FVector& GetDragPreviewLocal() const
	{
		return DragPreviewLocal;
	}

	/** Where an ADD would land right now, in MODEL space; only meaningful while GetAddIntent() and
	 *  nothing is hovered. */
	bool HasAddPreview() const
	{
		return bHasAddPreview;
	}

	const FVector& GetAddPreviewLocal() const
	{
		return AddPreviewLocal;
	}

	/** Vertex the dragged one would MERGE into on release (clusters cannot hold collocated vertices);
	 *  INDEX_NONE when the drop point is clear. Drawn as the merge highlight. */
	int32 GetMergeCandidate() const
	{
		return MergeCandidateVertex;
	}

	bool HasSelection() const
	{
		return !SelectedVertices.IsEmpty() || !SelectedEdges.IsEmpty();
	}

private:
	//~ Internals (model space)
	/** READ-ONLY view of the model. TSharedRef::operator-> hands back a non-const target, so every read
	 *  path must ask for constness explicitly -- otherwise a host that is read-only for AUTHORING (a
	 *  component instancing an asset) returns null and inspection dies along with editing. */
	const FPCGExClusterSketchModel* GetReadModel() const;

	//~ Selection. IDS are the source of truth -- they survive every reorder, removal and undo -- and the
	//~ index sets the draw helper, panel and mesh layer read are a cache, re-derived by
	//~ ResolveSelectionIndices whenever the model may have moved under them.
	/** Select a vertex and record it as the most recent one: gestures anchor on "last selected", which
	 *  a TSet cannot answer (its iteration follows sparse-array slots, not selection order). */
	void SelectVertex(int32 VertexIndex);
	void SelectEdge(int32 EdgeIndex);
	void DeselectVertex(int32 VertexIndex);
	void DeselectEdge(int32 EdgeIndex);
	/** Rebuild the index caches from the ids, dropping ids the model no longer carries. */
	void ResolveSelectionIndices();

	FRay ToLocal(const FRay& WorldRay) const;
	FPCGExSketchHit HitTestLocal(const FRay& LocalRay) const;
	/** Screen-constant pick cone, floored so picking is never TIGHTER than what is drawn: a mesh marker
	 *  keeps a fixed WORLD radius that the cone undercuts at close range. */
	double PickRadiusAt(const FRay& LocalRay, const FVector& LocalPos, double InMinWorldRadius = 0.0) const;

	/** World radius each kind actually draws at, or 0 when it falls back to immediate mode -- a
	 *  screen-space dot has no world footprint to match. Read from the shared style settings, the same
	 *  object the drawing reads: picking and drawing must never disagree about how big something is. */
	static double VertexPickFloor();
	static double GhostPickFloor();
	static double EdgePickFloor();
	FVector VertexLocation(const FPCGExClusterSketchVertex& V, const FPCGExLatticeBasis* Basis) const;

	/** Which gesture the placement solver is currently anchored for. */
	enum class EPlacementGesture : uint8
	{
		None,
		Add,
		Move,
		Connect
	};

	/** Point the solver at a gesture. Re-anchors -- which DROPS the guide latch -- only when the gesture
	 *  or its anchor actually changed, so a hover keeps its hysteresis from frame to frame. */
	void EnsurePlacementGesture(EPlacementGesture InGesture, int32 InAnchorVertex, const FVector& InAnchor, const FPCGExLatticeBasis* Basis, bool bPointWillSnap);

	/** Everything an add gesture resolves to, previewed and committed through the same call so the two
	 *  can never disagree about where the vertex goes. */
	struct FAddPlacement
	{
		FVector Point = FVector::ZeroVector;
		/** Anchor's lattice coord, donated to the new vertex so an add in a rank-collapsed basis stays
		 *  on the anchor's hidden layer. Only when bHasAnchorLayer. */
		FIntVector AnchorCoord = FIntVector::ZeroValue;
		bool bHasAnchorLayer = false;
		bool bResolved = false;
	};

	FAddPlacement ResolveAddPlacement(const FRay& LocalRay, const FPCGExLatticeBasis* Basis);

	/** The drag body, against a MODEL-space ray. Split out so a modifier or key change can re-run it
	 *  against the cached ray, which is the only one those events carry. */
	void ApplyDrag(const FRay& LocalRay);
	/** Nearest vertex (excluding IgnoreVertex) whose resolved location sits within merge reach of
	 *  LocalPoint -- pick radius, capped below half a cell so it can never bridge adjacent lattice
	 *  nodes. Drives merge-on-drop and place-reuse. LayerRef breaks projection stacks: under a
	 *  rank-collapsed basis many vertices resolve to one spot, and a candidate sharing LayerRef's
	 *  UNSPANNED coord components (the gesture source's layer) wins over a merely-nearest one. */
	int32 FindNearbyVertex(const FRay& LocalRay, const FVector& LocalPoint, int32 IgnoreVertex, const FPCGExLatticeBasis* Basis, const FIntVector* LayerRef = nullptr) const;
	void EndTransaction();

	/** Run the model's constraint solve inside the CURRENT transaction and refresh the residual cache.
	 *  Every mutation ends with this before NotifyModelChanged; a drag frame runs it per update. */
	void SolveConstraints(const FPCGExLatticeBasis* Basis, TConstArrayView<uint32> InPinnedIds = {});
	void RefreshConstraintResiduals();
	/** Which of the vertices a gesture holds stay pinned through the solve: those no constraint names
	 *  DIRECTLY. A direct subject (an Along) is projected instead -- the hand proposes, the constraint
	 *  disposes -- while an edge-reached one (a Length endpoint) follows the hand. */
	void GatherDragPins(TConstArrayView<int32> InHeldVertices, TArray<uint32>& OutPinnedIds) const;
	/** Snap a proposed point for a vertex: bound vertices re-snap through the basis, free ones only when
	 *  snapping is on. Writes the vertex; returns what it got. */
	FVector CommitProposedLocation(int32 VertexIndex, const FVector& Proposed, const FPCGExLatticeBasis* Basis);

	void OnObjectTransacted(UObject* InObject, const FTransactionObjectEvent& InEvent);
	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent);

	TSharedRef<IPCGExSketchEditTarget> Target;

	FDelegateHandle TransactedHandle;
	FDelegateHandle PropertyChangedHandle;
	/** Host notifications re-enter through the property-change delegate; the echo is skipped. */
	bool bNotifying = false;

	/** See GetModelRevision. */
	int32 ModelRevision = 0;

	/** Most recently selected vertex, or INDEX_NONE; the index cache of LastSelectedVertexId. */
	int32 LastSelectedVertex = INDEX_NONE;
	uint32 LastSelectedVertexId = 0;

	TSet<uint32> SelectedVertexIds;
	TSet<uint32> SelectedEdgeIds;

	/** Model shape the cached Crossings were derived from. Recomputing them is O(E^2), and hovering
	 *  cannot change the model, so the sweep only re-runs when this fingerprint moves. */
	int32 CrossingsRevision = INDEX_NONE;
	int32 CrossingsVertexCount = INDEX_NONE;
	int32 CrossingsEdgeCount = INDEX_NONE;

	/** Index caches -- see ResolveSelectionIndices. */
	TSet<int32> SelectedVertices;
	TSet<int32> SelectedEdges;
	FPCGExSketchHit Hover;

	/** Ghost crossings, refreshed after every mutation (indices into Model.Edges, valid until then). */
	TArray<FPCGExClusterSketchCrossing> Crossings;

	EDragMode DragMode = EDragMode::None;
	int32 DragVertexIndex = INDEX_NONE;
	int32 DragTargetVertexIndex = INDEX_NONE;
	int32 MergeCandidateVertex = INDEX_NONE;
	FVector DragPreviewLocal = FVector::ZeroVector;

	/** MoveEdge: the edge, its endpoints' positions at press, and the placement point at press -- the
	 *  delta from that point is what both endpoints receive. */
	int32 DragEdgeIndex = INDEX_NONE;
	FVector DragEdgeStartA = FVector::ZeroVector;
	FVector DragEdgeStartB = FVector::ZeroVector;
	FVector DragStartPoint = FVector::ZeroVector;

	/** See GetDragProposal. */
	TArray<FVector, TInlineAllocator<2>> DragProposalLocal;

	TMap<uint32, FPCGExSketchConstraintResidual> ConstraintResiduals;
	/** Basis snapshot for the duration of one drag, so mid-drag provider edits can't tear it. */
	FPCGExLatticeBasis DragBasis;
	bool bDragHasBasis = false;

	TUniquePtr<FScopedTransaction> ActiveTransaction;

	/** Cursor ray to work plane and guides, for every gesture. */
	FPCGExSketchPlacementSolver Placement;
	EPlacementGesture PlacementGesture = EPlacementGesture::None;
	int32 PlacementAnchorVertex = INDEX_NONE;
	FVector PlacementAnchor = FVector::ZeroVector;

	/** Last cursor ray in MODEL space. A key or modifier event reaches a live gesture carrying no valid
	 *  mouse data, so re-resolving one has nothing else to aim at. */
	FRay LastLocalRay;
	bool bHasLastLocalRay = false;

	FVector AddPreviewLocal = FVector::ZeroVector;
	bool bHasAddPreview = false;

	bool bSnapEnabled = true;
	bool bConnectToHover = true;
	bool bDeleteIntent = false;
	bool bAddIntent = false;
};
