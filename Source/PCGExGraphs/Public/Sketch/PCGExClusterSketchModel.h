// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Lattice/PCGExLatticeBasis.h"

#include "PCGExClusterSketchModel.generated.h"

/** Value type of one annotation channel. Enumerator identifiers are a wire format -- never rename. */
UENUM(BlueprintType)
enum class EPCGExClusterDataChannelType : uint8
{
	Double  = 0 UMETA(ToolTip = "Double-precision scalar."),
	Integer = 1 UMETA(ToolTip = "64-bit integer."),
	Name    = 2 UMETA(ToolTip = "FName."),
	Vector  = 3 UMETA(ToolTip = "3D vector."),
};

/**
 * One named, typed annotation channel over a sketch domain (vertices or edges, decided by which array
 * holds it). Uninterpreted by core -- clients own their channels by naming convention. At print time a
 * channel becomes a PCG attribute of the same name on the printed Vtx or Edges output.
 * Only the array matching Type is used; element count is kept aligned with the domain by the model's
 * mutation API.
 */
USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExClusterDataChannel
{
	GENERATED_BODY()

	/** Attribute name on the printed output. Reserved cluster attributes (PCGEx/VData, PCGEx/EData) are refused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExClusterDataChannelType Type = EPCGExClusterDataChannelType::Double;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Type == EPCGExClusterDataChannelType::Double", EditConditionHides))
	TArray<double> DoubleValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Type == EPCGExClusterDataChannelType::Integer", EditConditionHides))
	TArray<int64> IntegerValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Type == EPCGExClusterDataChannelType::Name", EditConditionHides))
	TArray<FName> NameValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "Type == EPCGExClusterDataChannelType::Vector", EditConditionHides))
	TArray<FVector> VectorValues;

	/** Element count of the active array. */
	int32 Num() const;

	/** Resize the active array, default-initializing new elements. Clears the inactive arrays. */
	void SetNumDefaulted(int32 InNum);

	void RemoveAt(int32 Index);
	void InsertDefaulted(int32 Index);
};

/** One authored sketch vertex. */
USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExClusterSketchVertex
{
	GENERATED_BODY()

	/** Authoritative for FREE vertices. For lattice-bound ones the LOCATION is derived from LatticeCoord
	 *  (rotation/scale stay authored); a hand-edited location re-snaps the coord instead of dangling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FTransform Transform = FTransform::Identity;

	/** Authoritative when bLatticeBound: the vertex IS this lattice node; world location derives from it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (EditCondition = "bLatticeBound", EditConditionHides))
	FIntVector LatticeCoord = FIntVector::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bLatticeBound = false;

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
struct PCGEXGRAPHS_API FPCGExClusterSketchEdge
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int32 A = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int32 B = -1;
};

/** Aggregate result of FPCGExClusterSketchModel::Validate -- counts, never element indices, so the
 *  caller can warn once per issue class. */
struct PCGEXGRAPHS_API FPCGExClusterSketchValidation
{
	int32 InvalidEdges = 0;      // out-of-range vertex index
	int32 SelfLoops = 0;
	int32 DuplicateEdges = 0;    // undirected duplicates beyond the first occurrence
	int32 IsolatedVertices = 0;  // dropped by cluster compile (clusters cannot represent them)
	/** Vertices sharing an earlier vertex's lattice coord (bound) or position (free) -- clusters cannot
	 *  carry collocated vertices; the editor merges on drop, raw edits get warned at print. */
	int32 CollocatedVertices = 0;
	/** PER DOMAIN: a channel name is only meaningful within its own domain, so a broken edge channel
	 *  must never suppress a healthy vertex channel that happens to share its name. */
	struct FChannelIssues
	{
		TArray<FName> Misaligned;   // channel length != domain count
		TArray<FName> InvalidNames; // None, duplicate, or reserved cluster attribute

		bool IsEmpty() const { return Misaligned.IsEmpty() && InvalidNames.IsEmpty(); }
		bool Rejects(const FName InName) const { return InvalidNames.Contains(InName) || Misaligned.Contains(InName); }
	};

	FChannelIssues VertexChannelIssues;
	FChannelIssues EdgeChannelIssues;

	bool HasEdgeIssues() const { return InvalidEdges > 0 || SelfLoops > 0 || DuplicateEdges > 0; }
	bool HasChannelIssues() const { return !VertexChannelIssues.IsEmpty() || !EdgeChannelIssues.IsEmpty(); }
};

/** One hypothetical crossing: two edges sharing a point that is not a vertex. Offered as a ghost in the
 *  editor and materialized on demand -- never cut automatically, since an X-brace with no joint is
 *  legitimate authoring. Edge indices are only valid until the model is mutated. */
struct PCGEXGRAPHS_API FPCGExClusterSketchCrossing
{
	int32 EdgeA = INDEX_NONE;
	int32 EdgeB = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
};

/**
 * The authored cluster-sketch model: vertices + undirected edges + annotation channels, all plain
 * serialized arrays. Edges reference vertices by array index; ALL mutations must go through the API
 * below, which keeps edge indices and channel arrays aligned through removals. Raw array edits (details
 * panel) are a supported surface -- print-time validation and the owning asset's edit hooks absorb them.
 */
USTRUCT(BlueprintType)
struct PCGEXGRAPHS_API FPCGExClusterSketchModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterSketchVertex> Vertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterSketchEdge> Edges;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterDataChannel> VertexChannels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterDataChannel> EdgeChannels;

	int32 NumVertices() const { return Vertices.Num(); }
	int32 NumEdges() const { return Edges.Num(); }

	/** THE location rule, in one place: a bound vertex resolves through the basis (when one exists),
	 *  a free one through its transform. */
	static FVector ResolvedLocation(const FPCGExClusterSketchVertex& V, const FPCGExLatticeBasis* Basis);

	/** Append a free vertex. Extends every vertex channel. @return the new vertex index. */
	int32 AddVertex(const FTransform& InTransform);

	/** Append a lattice-bound vertex at Coord; location derived through the basis. Extends channels. */
	int32 AddLatticeVertex(const FIntVector& InCoord, const FPCGExLatticeBasis& InBasis);

	/** Remove a vertex, its edges, and its channel entries; remaining edge indices are remapped. */
	bool RemoveVertex(int32 Index);

	/** Add the undirected edge (A,B). Idempotent: an existing edge is returned rather than duplicated.
	 *  @param bOutCreated set true only when a NEW edge was appended (channel entries defaulted).
	 *  @return the edge index, or INDEX_NONE for an invalid pair (out of range or self-loop). */
	int32 Connect(int32 A, int32 B, bool* bOutCreated = nullptr);

	/** Remove the undirected edge (A,B) and its channel entries. @return true if an edge was removed. */
	bool Disconnect(int32 A, int32 B);

	/** Remove one edge BY INDEX. Callers holding edge indices must use this: Disconnect resolves through
	 *  FindEdge, which returns the first pair match and so removes the wrong duplicate. */
	bool RemoveEdgeAt(int32 EdgeIndex);

	/**
	 * Merge InAbsorbed into InSurvivor: every edge of the absorbed vertex retargets its endpoint onto
	 * the survivor (edges that would become self-loops or duplicates are dropped, the survivor edge's
	 * channel values winning), then the absorbed vertex and its channel entries are removed. The
	 * editor's drop-on-vertex gesture and any raw cleanup both go through this.
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
	 * connectivity that is already there. Every NEW segment inherits the parent edge's channel values.
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
	 * (first occurrence kept) -- with their channel entries. Out-of-range edges are DORMANT hazards:
	 * invisible and unprintable today, they silently reactivate the moment the vertex array grows past
	 * their indices, materializing as "random" edges onto new vertices. Never called automatically (a
	 * details-panel edit passes through invalid states mid-typing); the editor surfaces them and the
	 * sketch's cleanup button invokes this deliberately.
	 * @return the number of edges removed.
	 */
	int32 RemoveInvalidEdges();

	/** Aggregate integrity summary; cheap, never mutates. */
	void Validate(FPCGExClusterSketchValidation& OutSummary) const;
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
	PCGEXGRAPHS_API bool SegmentsCross(const FVector& A1, const FVector& B1, const FVector& A2, const FVector& B2, FVector& OutPoint);
}
