// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExClusterSketchConstraint.generated.h"

struct FPCGExClusterSketchModel;
struct FPCGExLatticeBasis;

namespace PCGExSketch
{
	/** A subject count meaning "any number" -- the type accepts however many ids its Subjects array holds. */
	inline constexpr int32 VariadicSubjects = INDEX_NONE;

	/** Residual at or below which a constraint counts as satisfied. Shares the model's coincidence scale. */
	inline constexpr double ConstraintTolerance = 0.05;

	/** Subject slots of FPCGExSketchConstraint_Along. */
	namespace AlongRole
	{
		inline constexpr int32 Subject = 0;
		inline constexpr int32 AnchorA = 1;
		inline constexpr int32 AnchorB = 2;
	}
}

/**
 * Working state of one solve (or one residual evaluation): every vertex's MODEL-space position, resolved
 * through the basis for bound vertices, plus id lookups. Constraint types read and write positions only
 * through this; the model is written back once the passes are done.
 */
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchSolveContext
{
	const FPCGExClusterSketchModel* Model = nullptr;
	const FPCGExLatticeBasis* Basis = nullptr;

	/** Parallel to Model->Vertices. */
	TArray<FVector> Positions;

	/** Parallel to Positions: true for vertices the solver may move -- a subject of some enabled
	 *  constraint, or an endpoint of a subject edge. Everything else is infinite mass. */
	TArray<bool> bMovable;

	/** Vertices a gesture holds; never moved, whatever the constraints say. */
	TSet<uint32> PinnedIds;

	TMap<uint32, int32> VertexIndexById;
	TMap<uint32, int32> EdgeIndexById;

	void Build(const FPCGExClusterSketchModel& InModel, const FPCGExLatticeBasis* InBasis, TConstArrayView<uint32> InPinnedIds);

	int32 VertexIndex(const uint32 InId) const
	{
		const int32* Found = VertexIndexById.Find(InId);
		return Found ? *Found : INDEX_NONE;
	}

	int32 EdgeIndex(const uint32 InId) const
	{
		const int32* Found = EdgeIndexById.Find(InId);
		return Found ? *Found : INDEX_NONE;
	}

	bool CanMove(const int32 VertexIdx) const;
};

/** Which kind of element a subject slot names. */
UENUM()
enum class EPCGExSketchSubjectKind : uint8
{
	Vertex = 0,
	Edge   = 1,
};

/**
 * One authoring constraint: a relation over element IDS that the solver keeps true by projecting its
 * subjects. Editor-only in effect -- the solver writes concrete positions into the model, and that is
 * all a print ever reads. Lives in FPCGExClusterSketchModel::Constraints; list order is priority (a
 * later entry projects later, so it wins a conflict).
 *
 * A new constraint is a new USTRUCT deriving from this; nothing in the model changes. Types may live in
 * any module -- a snap provider from a sibling plugin can hand the sketch its own binding constraint.
 *
 * Subjects are held in ROLE ORDER (GetSubjectKind / GetRoleName describe each slot). A type declaring
 * VariadicSubjects takes any number, all of the kind slot 0 declares.
 */
USTRUCT()
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchConstraint
{
	GENERATED_BODY()

	virtual ~FPCGExSketchConstraint() = default;

	/** Minted from the model's element counter: a constraint is an element of the sketch too. */
	UPROPERTY()
	uint32 Id = 0;

	/** Disabled constraints are skipped by the solver and the validation alike, but keep their subjects. */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bEnabled = true;

	/** Element ids, in role order. */
	UPROPERTY()
	TArray<uint32> Subjects;

	/** Fixed count, or PCGExSketch::VariadicSubjects. */
	virtual int32 GetNumSubjects() const
	{
		return 0;
	}

	virtual EPCGExSketchSubjectKind GetSubjectKind(int32 Slot) const
	{
		return EPCGExSketchSubjectKind::Vertex;
	}

	virtual FText GetRoleName(int32 Slot) const
	{
		return FText::GetEmpty();
	}

	virtual FText GetDisplayName() const
	{
		return FText::GetEmpty();
	}

	/** True when every subject slot resolves in the context. A constraint that does not is skipped and
	 *  reported as dangling. */
	bool ResolvesIn(const FPCGExSketchSolveContext& Ctx) const;

	/**
	 * Whether this constraint is ABOUT one of the given elements: a vertex it moves (not one it merely
	 * reads -- an Along's anchors do not count), or an edge it names. THE selection filter, shared by
	 * the panel and any context menu, so "this element's constraints" means one thing everywhere.
	 */
	bool Concerns(const FPCGExSketchSolveContext& Ctx, TConstArrayView<uint32> InVertexIds, TConstArrayView<uint32> InEdgeIds) const;

	/** Every vertex this constraint MOVES -- what Project writes. Feeds bMovable, and is what the
	 *  warning ring marks when the residual is non-zero. Default: every vertex subject plus the endpoints
	 *  of every edge subject; a type whose subjects include references it only READS (an Along's
	 *  anchors) must narrow this, or those references become movable for every other constraint too. */
	virtual void GatherMovableVertices(const FPCGExSketchSolveContext& Ctx, TArray<int32>& OutVertexIndices) const;

	/** Move the movable, unpinned subjects onto this constraint's feasible set. Called once per pass. */
	virtual void Project(FPCGExSketchSolveContext& Ctx) const
	{
	}

	/** Distance from satisfied, in model units; 0 when satisfied. Read-only. */
	virtual double Residual(const FPCGExSketchSolveContext& Ctx) const
	{
		return 0.0;
	}

	/** Called when the constraint is first attached, so type parameters can be seeded from the geometry
	 *  as it stands (an Along takes its parameter from where the vertex already sits). */
	virtual void InitializeFromGeometry(const FPCGExSketchSolveContext& Ctx)
	{
	}

	/**
	 * A gesture holds one of this constraint's DIRECT vertex subjects and proposes a position for it.
	 * Re-parameterise so the proposal is honoured as far as this constraint allows (an Along projects it
	 * onto the span and takes the fraction); the solve then runs as usual. False = nothing to absorb,
	 * the stored parameters stand. Only called for enabled constraints that resolve.
	 */
	virtual bool AbsorbProposal(const FPCGExSketchSolveContext& Ctx, uint32 InVertexId, const FVector& InProposed)
	{
		return false;
	}

	/**
	 * THE authoring entry: fill Subjects from what the user selected, inferring whatever the selection
	 * does not name (an Along infers its anchors from the chain). False when the selection does not fit
	 * this type -- which is also how a picker decides whether to OFFER it. Must not mutate the model.
	 */
	virtual bool BuildSubjectsFromSelection(const FPCGExClusterSketchModel& InModel, TConstArrayView<uint32> InSelectedVertexIds, TConstArrayView<uint32> InSelectedEdgeIds)
	{
		return false;
	}
};

namespace PCGExSketch
{
	/** Every concrete constraint type, from reflection -- a sibling plugin's USTRUCT shows up by existing.
	 *  Deterministic order (by name) so pickers are stable. */
	PCGEXELEMENTSCLUSTERSSKETCH_API void GatherConstraintTypes(TArray<const UScriptStruct*>& OutTypes);
}

/**
 * The subject vertex lies on the segment between two anchor vertices, at a fraction of its length or a
 * fixed distance from one end. The segment is a PHANTOM edge: it needs no materialized edge between
 * the anchors, and is drawn as one while the subject is selected.
 */
USTRUCT(DisplayName = "Along")
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchConstraint_Along : public FPCGExSketchConstraint
{
	GENERATED_BODY()

	/** Place by fraction of the anchor span, else by a fixed distance from an anchor. */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bByFraction = true;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (EditCondition = "bByFraction", ClampMin = "0.0", ClampMax = "1.0"))
	double Fraction = 0.5;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (EditCondition = "!bByFraction", ClampMin = "0.0"))
	double Distance = 100.0;

	/** Measure Distance from anchor B instead of anchor A. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (EditCondition = "!bByFraction"))
	bool bFromB = false;

	virtual int32 GetNumSubjects() const override
	{
		return 3;
	}

	virtual FText GetRoleName(int32 Slot) const override;
	virtual FText GetDisplayName() const override;
	virtual void Project(FPCGExSketchSolveContext& Ctx) const override;
	virtual double Residual(const FPCGExSketchSolveContext& Ctx) const override;
	virtual void InitializeFromGeometry(const FPCGExSketchSolveContext& Ctx) override;
	/** The subject only: the anchors are read, never moved, by this constraint. */
	virtual void GatherMovableVertices(const FPCGExSketchSolveContext& Ctx, TArray<int32>& OutVertexIndices) const override;
	/** Sliding the subject moves the parameter, never fights it. */
	virtual bool AbsorbProposal(const FPCGExSketchSolveContext& Ctx, uint32 InVertexId, const FVector& InProposed) override;
	/** One vertex, no edges; anchors inferred along its chain. */
	virtual bool BuildSubjectsFromSelection(const FPCGExClusterSketchModel& InModel, TConstArrayView<uint32> InSelectedVertexIds, TConstArrayView<uint32> InSelectedEdgeIds) override;

	/** Where the subject should be for the anchors as they stand; false when a slot fails to resolve. */
	bool Target(const FPCGExSketchSolveContext& Ctx, FVector& OutTarget) const;
};

/** The subject edge keeps a fixed length. Both endpoints move symmetrically when both may move; one
 *  moves alone when the other is fixed or pinned. */
USTRUCT(DisplayName = "Length")
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExSketchConstraint_Length : public FPCGExSketchConstraint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin = "0.0"))
	double Length = 100.0;

	virtual int32 GetNumSubjects() const override
	{
		return 1;
	}

	virtual EPCGExSketchSubjectKind GetSubjectKind(int32 Slot) const override
	{
		return EPCGExSketchSubjectKind::Edge;
	}

	virtual FText GetRoleName(int32 Slot) const override;
	virtual FText GetDisplayName() const override;
	virtual void Project(FPCGExSketchSolveContext& Ctx) const override;
	virtual double Residual(const FPCGExSketchSolveContext& Ctx) const override;
	virtual void InitializeFromGeometry(const FPCGExSketchSolveContext& Ctx) override;
	/** One edge, no vertices. */
	virtual bool BuildSubjectsFromSelection(const FPCGExClusterSketchModel& InModel, TConstArrayView<uint32> InSelectedVertexIds, TConstArrayView<uint32> InSelectedEdgeIds) override;
};

/** Residual of one constraint after a solve or an evaluation. */
struct FPCGExSketchConstraintResidual
{
	uint32 ConstraintId = 0;
	double Residual = 0.0;
	bool bDangling = false;

	bool IsSatisfied() const
	{
		return !bDangling && Residual <= PCGExSketch::ConstraintTolerance;
	}
};
