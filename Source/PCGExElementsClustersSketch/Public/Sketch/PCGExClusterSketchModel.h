// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Lattice/PCGExLatticeBasis.h"
#include "Sketch/PCGExClusterSketchData.h"

#include "PCGExClusterSketchModel.generated.h"

namespace PCGExSketch
{
	/** Element id value meaning "not yet minted" -- what a raw details-panel row add or a pre-id asset carries. */
	inline constexpr uint32 InvalidElementId = 0;
}

/** One authored sketch vertex. */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchVertex
{
	GENERATED_BODY()

	/** Identity that survives every reorder and removal, unlike the array index. Minted by the model from
	 *  its counter; never reused. Bare UPROPERTY so the details panel cannot hand-edit it. */
	UPROPERTY()
	uint32 Id = PCGExSketch::InvalidElementId;

	/** Authoritative for FREE vertices. For lattice-bound ones the LOCATION is derived from LatticeCoord
	 *  (rotation/scale stay authored); a hand-edited location re-snaps the coord instead of dangling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FTransform Transform = FTransform::Identity;

	/** Authoritative when bLatticeBound: the vertex IS this lattice node; world location derives from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "bLatticeBound", EditConditionHides))
	FIntVector LatticeCoord = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bLatticeBound = false;

	/** Record this vertex reads its authored values from; invalid (the default) resolves every field to
	 *  the schema's own value. Duplicate ids across vertices are LEGAL -- never de-duplicate. */
	UPROPERTY()
	uint32 DataId = PCGExSketch::InvalidRecordId;

#if WITH_EDITORONLY_DATA
	/** Authoring provenance: true for vertices the TOOL inserted (edge splits at crossings) rather than
	 *  the hand. Stays true for life; an edge removal that leaves a side-effect vertex isolated removes
	 *  it in the same operation. Never printed. */
	UPROPERTY(VisibleAnywhere, Category = Settings)
	bool bSideEffect = false;
#endif
};

/** One authored sketch edge -- a pair of vertex array indices, undirected. */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchEdge
{
	GENERATED_BODY()

	/** See FPCGExClusterSketchVertex::Id. Shares the vertex counter, so an id names an element of either kind. */
	UPROPERTY()
	uint32 Id = PCGExSketch::InvalidElementId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int32 A = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int32 B = -1;

	/** See FPCGExClusterSketchVertex::DataId. Splitting an edge hands every NEW segment the parent's
	 *  id, so one record still describes the whole original span. */
	UPROPERTY()
	uint32 DataId = PCGExSketch::InvalidRecordId;
};

/** Aggregate result of FPCGExClusterSketchModel::Validate -- counts, never element indices, so the
 *  caller can warn once per issue class. */
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchValidation
{
	int32 InvalidEdges = 0; // out-of-range vertex index
	int32 SelfLoops = 0;
	int32 DuplicateEdges = 0;   // undirected duplicates beyond the first occurrence
	int32 IsolatedVertices = 0; // dropped by cluster compile (clusters cannot represent them)
	/** Vertices sharing an earlier vertex's lattice coord (bound) or position (free) -- clusters cannot
	 *  carry collocated vertices; the editor merges on drop, raw edits get warned at print. */
	int32 CollocatedVertices = 0;

	/** PER LAYER: a property name is only meaningful within its own domain, so a broken edge entry must
	 *  never suppress a healthy vertex entry that happens to share its name. */
	struct FLayerIssues
	{
		/** Schema entries rejected for OUTPUT: the name is None after sanitization, collides with
		 *  another entry's sanitized name, or is a reserved cluster attribute. Keyed by the SCHEMA
		 *  name, which is what the print path holds. */
		TArray<FName> InvalidNames;
		/** Items whose DataId is set but names no record -- printed as schema defaults. */
		int32 DanglingRefs = 0;
		/** Records sharing an Id; only the first is ever addressable. */
		int32 DuplicateRecordIds = 0;

		bool IsEmpty() const
		{
			return InvalidNames.IsEmpty() && DanglingRefs == 0 && DuplicateRecordIds == 0;
		}

		bool Rejects(const FName InName) const
		{
			return InvalidNames.Contains(InName);
		}
	};

	FLayerIssues SketchLayerIssues;
	FLayerIssues VertexLayerIssues;
	FLayerIssues EdgeLayerIssues;

	bool HasEdgeIssues() const
	{
		return InvalidEdges > 0 || SelfLoops > 0 || DuplicateEdges > 0;
	}

	bool HasLayerIssues() const
	{
		return !SketchLayerIssues.IsEmpty() || !VertexLayerIssues.IsEmpty() || !EdgeLayerIssues.IsEmpty();
	}
};

/** One hypothetical crossing: two edges sharing a point that is not a vertex. Offered as a ghost in the
 *  editor and materialized on demand -- never cut automatically, since an X-brace with no joint is
 *  legitimate authoring. Edge indices are only valid until the model is mutated. */
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchCrossing
{
	int32 EdgeA = INDEX_NONE;
	int32 EdgeB = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
};

/**
 * The authored cluster-sketch model: vertices + undirected edges + the authored data tier, all plain
 * serialized arrays. Edges reference vertices by array index; ALL mutations must go through the API
 * below, which keeps edge indices and record references coherent through removals. Raw array edits (details
 * panel) are a supported surface -- print-time validation and the owning asset's edit hooks absorb them.
 *
 * Every mutation here is PURE: the host owns the transaction, the change notify, and the schema sync.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterSketchVertex> Vertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterSketchEdge> Edges;

	/** The authored tier. Travels with the model through Save To Asset / Create Inline Sketch, and the
	 *  two sharing rules (merge-inherit, split-share) sit next to the mutations that apply them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGExSketchData Data;

	/** Next element id to mint. Serialized with the model so ids stay unique across sessions; a counter
	 *  rather than a GUID because it is deterministic and cheap to hash. */
	UPROPERTY()
	uint32 NextElementId = 1;

	/** Array index of the element carrying InId, or INDEX_NONE. Linear -- ids are for holding identity
	 *  across edits, not for per-frame lookup; a consumer needing that builds its own map per revision. */
	int32 FindVertexIndex(uint32 InId) const;
	int32 FindEdgeIndex(uint32 InId) const;

	/**
	 * Give every element with an unminted or duplicated id a fresh one (first holder wins), and keep the
	 * counter ahead of every id in use -- raw row edits and pre-id assets both reach here. Runtime-safe;
	 * hosts call it from PostLoad and from their edit hooks. @return the number re-minted.
	 */
	int32 RepairElementIds();

	int32 NumVertices() const
	{
		return Vertices.Num();
	}

	int32 NumEdges() const
	{
		return Edges.Num();
	}

	/** THE location rule, in one place: a bound vertex resolves through the basis (when one exists),
	 *  a free one through its transform. */
	static FVector ResolvedLocation(const FPCGExClusterSketchVertex& V, const FPCGExLatticeBasis* Basis);

	/** Extent of the resolved vertex locations -- bound vertices measure where they PRINT, not where
	 *  their transform says. Invalid (ForceInit) box when there are no vertices. */
	FBox GetBounds(const FPCGExLatticeBasis* Basis) const;

	/** Append a free vertex. InDataId is the record it reads from, defaulting to none.
	 *  @return the new vertex index. */
	int32 AddVertex(const FTransform& InTransform, uint32 InDataId = PCGExSketch::InvalidRecordId);

	/** Append a lattice-bound vertex at Coord; location derived through the basis. InDataId as AddVertex. */
	int32 AddLatticeVertex(const FIntVector& InCoord, const FPCGExLatticeBasis& InBasis, uint32 InDataId = PCGExSketch::InvalidRecordId);

	/** Remove a vertex and its edges; remaining edge indices are remapped. Records are left alone --
	 *  an unreferenced one is purged deliberately, never as a side effect. */
	bool RemoveVertex(int32 Index);

	/** Add the undirected edge (A,B). Idempotent: an existing edge is returned rather than duplicated.
	 *  @param bOutCreated set true only when a NEW edge was appended (referencing no record).
	 *  @return the edge index, or INDEX_NONE for an invalid pair (out of range or self-loop). */
	int32 Connect(int32 A, int32 B, bool* bOutCreated = nullptr);

	/** Remove the undirected edge (A,B). @return true if an edge was removed. */
	bool Disconnect(int32 A, int32 B);

	/** Remove one edge BY INDEX. Callers holding edge indices must use this: Disconnect resolves through
	 *  FindEdge, which returns the first pair match and so removes the wrong duplicate. */
	bool RemoveEdgeAt(int32 EdgeIndex);

	/**
	 * Merge InAbsorbed into InSurvivor: every edge of the absorbed vertex retargets its endpoint onto
	 * the survivor (edges that would become self-loops or duplicates are dropped, the survivor edge's
	 * record winning), then the absorbed vertex is removed. The survivor INHERITS the absorbed vertex's
	 * record only when it has none of its own. The editor's drop-on-vertex gesture and any raw cleanup
	 * both go through this.
	 * @return the survivor's index AFTER the removal remap, or INDEX_NONE for an invalid pair.
	 */
	int32 MergeVertices(int32 InAbsorbed, int32 InSurvivor);

	/** Index of the undirected edge (A,B), or INDEX_NONE. */
	int32 FindEdge(int32 A, int32 B) const;

	/** Bind/unbind a vertex to the lattice. Binding snaps the coord from the current location and
	 *  re-derives the location; unbinding keeps the current derived location as the free position. */
	bool SetLatticeBound(int32 Index, bool bBound, const FPCGExLatticeBasis& InBasis);

	/** Re-derive every bound vertex's location from its coord (basis edits rescale the sketch), or
	 *  re-snap coords from locations first when bResnapFromLocation (hand-edited transforms). */
	void SyncBoundVertices(const FPCGExLatticeBasis& InBasis, bool bResnapFromLocation);

	/**
	 * First vertex whose resolved location sits strictly INSIDE the edge's segment (endpoint-coincident
	 * counts as collocation, not overlap), or INDEX_NONE. An edge through a vertex is degenerate for a
	 * cluster -- collinear A-B-C may carry A-B and B-C but never A-C.
	 */
	int32 FindVertexOnEdgeInterior(int32 EdgeIndex, const FPCGExLatticeBasis* Basis) const;

	/** Same test against locations the caller already resolved -- for per-frame consumers, where
	 *  re-deriving every vertex position inside the scan is the whole cost. */
	int32 FindVertexOnEdgeInterior(int32 EdgeIndex, TConstArrayView<FVector> InLocations) const;

	/**
	 * Replace an edge that passes through vertices with the chain of segments between them (sorted along
	 * the edge), deduping against existing edges -- so retarget-created degeneracies dissolve into the
	 * connectivity that is already there. Every NEWLY CREATED segment inherits the parent edge's record;
	 * a segment deduped onto a pre-existing edge keeps its own.
	 * @return the number of edges the chain replaced the original with (0 = nothing contained, untouched).
	 */
	int32 SplitEdgeByContainedVertices(int32 EdgeIndex, const FPCGExLatticeBasis* Basis, TArray<uint64>* OutSegmentKeys = nullptr);

	/**
	 * Restore edge/VERTEX separation around one vertex after a gesture: split every edge passing through
	 * it, and split the edges the gesture TOUCHED by any vertices they pass through. Runs to a fixed
	 * point. Crossings are deliberately NOT touched here -- they are offered as ghosts and materialized
	 * on demand (FindEdgeCrossings / MaterializeCrossing).
	 *
	 * Scope follows the touched edges THROUGH their own splits, tracked by endpoint-pair key: edge
	 * indices shift on every split, vertex indices never do (this only ever appends vertices).
	 * @return total splits performed.
	 */
	int32 EnforceSeparationAroundVertex(int32 VertexIndex, const FPCGExLatticeBasis* Basis);

	/** Full-model sweep of SplitEdgeByContainedVertices -- the explicit cleanup. @return total splits. */
	int32 SplitAllOverlappingEdges(const FPCGExLatticeBasis* Basis);

	/**
	 * Every hypothetical crossing in the model: pairs of edges meeting at a point that is not a vertex
	 * (adjacent edges, sharing a vertex, never count). THE crossing enumeration -- shared by the editor
	 * ghosts, the print warning, and the sweep below, so none of them can disagree.
	 */
	void FindEdgeCrossings(TArray<FPCGExClusterSketchCrossing>& OutCrossings, const FPCGExLatticeBasis* Basis) const;

	/**
	 * Materialize ONE crossing: insert a vertex at Location (side-effect provenance -- the tool placed
	 * the geometry, the user only chose when) and split both edges through it. The user-invoked commit
	 * behind the editor's ghost affordance.
	 * @return the new vertex index, or INDEX_NONE if the pair is no longer valid.
	 */
	int32 MaterializeCrossing(int32 EdgeA, int32 EdgeB, const FVector& Location, const FPCGExLatticeBasis* Basis);

	/**
	 * Materialize every crossing (the explicit cleanup). Rescans after each one, since materializing
	 * shifts edge indices. @return the number of crossing vertices inserted.
	 */
	int32 InsertCrossingVertices(const FPCGExLatticeBasis* Basis);

#if WITH_EDITORONLY_DATA
	/** Remove side-effect vertices left with no edges -- called by edge-removing operations so tool
	 *  residue never outlives the geometry that justified it. Hand-made vertices are never touched.
	 *  @return the number removed. */
	int32 RemoveOrphanSideEffectVertices();

	/** Clear a vertex's side-effect provenance: the user edited it deliberately, so it is theirs now.
	 *  Called from the AUTHORING layer only (gestures, details edits) -- never from structural repair,
	 *  whose internal Connect/split calls would otherwise promote the very vertices they insert. */
	void MarkVertexAuthored(int32 VertexIndex);
#endif

	/**
	 * What deleting InVertexIndex would take with it: the vertex itself, every edge touching it, and any
	 * side-effect vertex the removal would leave with no edges (mirroring the orphan sweep every
	 * edge-removing operation runs). Pure query -- authoring FEEDBACK, so both editing hosts advertise
	 * exactly what the gesture does, from one implementation.
	 */
	void GatherVertexRemovalCascade(int32 InVertexIndex, TSet<int32>& OutVertices, TSet<int32>& OutEdges) const;

	/**
	 * Drop every structurally invalid edge -- out-of-range endpoints, self-loops, undirected duplicates
	 * (first occurrence kept). Out-of-range edges are DORMANT hazards:
	 * invisible and unprintable today, they silently reactivate the moment the vertex array grows past
	 * their indices, materializing as "random" edges onto new vertices. Never called automatically (a
	 * details-panel edit passes through invalid states mid-typing); the editor surfaces them and the
	 * sketch's cleanup button invokes this deliberately. Tool residue the removal strands goes with it.
	 * @return the number of edges removed.
	 */
	int32 RemoveInvalidEdges();

	/**
	 * Merge every vertex that RESOLVES to an already-occupied printed location (duplicate coords,
	 * overlapping free positions, or a rank-collapsed snap basis projecting distinct coords together)
	 * into the earliest vertex there, then resolve the degeneracies the retargeting creates.
	 * @return the number of merges performed.
	 */
	int32 MergeCollocatedVertices(const FPCGExLatticeBasis* Basis);

	/**
	 * Resolve every edge overlap: an edge passing through a vertex splits into the chain between them,
	 * and two crossing edges gain a side-effect vertex at the crossing and split through it. The full
	 * cleanup, where SplitAllOverlappingEdges only does the containment half.
	 * @return chain splits plus crossing vertices inserted.
	 */
	int32 SplitOverlappingEdges(const FPCGExLatticeBasis* Basis);

	/**
	 * Record an edge extruded FROM InVertexIndex inherits: the one its source vertex's SOLE edge holds,
	 * so a drafted chain keeps a single description of its span. A junction (2+ edges) or a loose end
	 * (0) has no unambiguous parent and yields an invalid id.
	 *
	 * Resolve against the PRE-GESTURE model: the extruded edge itself would otherwise count toward the
	 * source's degree, and enforcement splits shift edges under any index a caller held.
	 */
	uint32 ResolveExtrudeEdgeDataId(int32 InVertexIndex) const;

	/** Point an item at a record (or at none with an invalid id). THE write path for DataId outside the
	 *  model's own sharing rules, so a future per-item invariant has one site. @return false if out of range. */
	bool SetVertexDataId(int32 Index, uint32 InDataId);
	bool SetEdgeDataId(int32 Index, uint32 InDataId);

	/** Every record id the model currently references, per domain -- including duplicates, so a caller
	 *  can count shares. Invalid ids are skipped. */
	void GatherLiveDataIds(TArray<uint32>& OutVertexIds, TArray<uint32>& OutEdgeIds) const;

	/** How many items reference InDataId. Drives the panel's "shared by N". */
	int32 CountVertexReferences(uint32 InDataId) const;
	int32 CountEdgeReferences(uint32 InDataId) const;

	/**
	 * Drop every record no item references, in both layers. NEVER automatic: PostSaveRoot is not
	 * transacted, so an auto-purge lets delete-vertex -> save -> undo resurrect an item pointing at a
	 * record that no longer exists. @return the number of records removed.
	 */
	int32 PurgeUnreferencedRecords();

	/** Aggregate integrity summary; cheap, never mutates. */
	void Validate(FPCGExClusterSketchValidation& OutSummary) const;

private:
	uint32 MintElementId();
};

namespace PCGExSketch
{
	/** The ONE geometric-coincidence tolerance: vertex collocation, vertex-on-edge-interior, and their
	 *  quantized hash keys all share it, so no two consumers can disagree about "same place". */
	constexpr double CoincidenceTolerance = 0.01;

	/** Hash/compare key for "these vertices resolve to the same printed location" -- shared by the
	 *  print warning, the editor highlight, and the Merge Collocated cleanup. */
	FORCEINLINE FVector QuantizedLocationKey(const FVector& Location)
	{
		constexpr double Scale = 1.0 / CoincidenceTolerance;
		return FVector(FMath::RoundToDouble(Location.X * Scale), FMath::RoundToDouble(Location.Y * Scale), FMath::RoundToDouble(Location.Z * Scale));
	}

	/**
	 * True when two segments CROSS: closest points within the coincidence tolerance and strictly
	 * interior on BOTH (an endpoint touching the other segment is the vertex-on-edge case, and touching
	 * endpoints are collocation -- neither is a crossing). OutPoint = the crossing location. The ONE
	 * crossing definition shared by insertion, the editor highlight, and the print warning.
	 */
	PCGEXELEMENTSCLUSTERSSKETCH_API bool SegmentsCross(const FVector& A1, const FVector& B1, const FVector& A2, const FVector& B2, FVector& OutPoint);
}
