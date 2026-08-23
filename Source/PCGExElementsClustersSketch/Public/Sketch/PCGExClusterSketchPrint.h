// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExClusterSketchModel.h"
#include "Lattice/PCGExLatticeBasis.h"

struct FPCGExContext;
class FPCGExSketchLayerPropertyProvider;
class UPCGExClusterSketchDecorator;
class UPCGExClusterSnapProvider;
struct FPCGExClusterSketchModel;
struct FPCGExGraphBuilderDetails;

namespace PCGExData
{
	class FFacade;
	class FPointIO;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExMT
{
	class FTaskManager;
}

/**
 * Shared state of one print: what decorators read, and what consumers use to map model elements onto
 * the printed output. Allocated by the caller, filled by PrintClusterSketch, alive until the compile's
 * end callback has run.
 */
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchPrintContext
{
	/** The printed model -- always &OwnedModel. A pointer so decorators read one shape whatever the host. */
	const FPCGExClusterSketchModel* Model = nullptr;

	/** The print's OWN copy of the model, taken synchronously at print start: the async compile and the
	 *  concurrent edge hooks then never read a host the editor may be mutating. */
	FPCGExClusterSketchModel OwnedModel;

	/** Snap-lattice basis, copied in (POD). Valid only when bHasBasis. */
	FPCGExLatticeBasis Basis;
	bool bHasBasis = false;

	/** Vtx facade. During the vertex-domain phase, point index == model vertex index. */
	TSharedPtr<PCGExData::FFacade> VtxFacade;

	/** Parent graph edge index -> model edge index (the print's validated, deduped edge order). */
	TArray<int32> ParentToModelEdge;

	/** Authored-tier read seam, indexed by MODEL vertex / model edge index. READ-ONLY once the print
	 *  reaches the graph build: DecorateEdges runs concurrently across subgraphs and shares this
	 *  context, so any mutation here -- memoization included -- is a data race. Always built; an
	 *  unannotated sketch resolves every name to its schema default. */
	TSharedPtr<const FPCGExSketchLayerPropertyProvider> VertexDataProvider;
	TSharedPtr<const FPCGExSketchLayerPropertyProvider> EdgeDataProvider;

	/** Model vertex index -> OUTPUT point index; INDEX_NONE for pruned (isolated) vertices.
	 *  Filled when the compile succeeds, BEFORE the request's OnCompiled fires. */
	TArray<int32> VtxIndexMap;
};

namespace PCGExSketch
{
	/** Inputs of one print. BuilderDetails and the decorator objects must outlive the async compile. */
	struct PCGEXELEMENTSCLUSTERSSKETCH_API FPrintRequest
	{
		/** Copied into the print context synchronously; need only outlive the PrintClusterSketch call. */
		const FPCGExClusterSketchModel* Model = nullptr;

		/** Optional snap basis (copied into the print context). */
		const FPCGExLatticeBasis* Basis = nullptr;

		/** Decorators to run, in order; null/disabled entries are skipped. */
		TArray<const UPCGExClusterSketchDecorator*> Decorators;

		const FPCGExGraphBuilderDetails* BuilderDetails = nullptr;

		/** Applied AFTER basis resolution, so a lattice coord resolves in sketch space and the whole
		 *  result is then placed. Identity prints in sketch space. */
		FTransform LocalToWorld = FTransform::Identity;

		/** Quiet the model-integrity warnings (dropped edges, isolated vertices, skipped schema entries). */
		bool bQuiet = false;

		/** Also write the sketch-level @Data properties onto every edges output. Off prints them on the
		 *  vtx output only. Authored values are UNIFORM across every duplicate of a sketch -- a consumer
		 *  duplicating the print builds no facade, so per-target variation exists only through tags or
		 *  whole-data forwarding. */
		bool bWriteSketchPropertiesToEdges = true;

		/** Fires from the compile's end callback, after VtxIndexMap is filled, with the success flag. */
		TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled;
	};

	/**
	 * Print a sketch model into InVtxIO (caller-emplaced, no points yet) and launch the cluster compile.
	 * Runs the whole §recipe: validate + dedup edges, allocate + write transforms/seeds, write the
	 * authored vertex layer + decorators, commit, build graph, hook the authored edge layer +
	 * decorators, CompileAsync.
	 *
	 * @return the builder (compile launched), or null when the model prints nothing (caller disables the
	 * vtx IO). On ASYNC failure (bCompiledSuccessfully false) the vtx IO still holds every point but no
	 * cluster data -- the caller must roll it back (InitializeOutput(NoInit) + Disable) when staging.
	 */
	/**
	 * Assemble a request from already-resolved pieces and print it -- the shared body behind every
	 * sketch HOST (the asset, the component). Builds the basis from InSnapProvider; the basis may be a
	 * local, since PrintClusterSketch consumes it synchronously and copies it into the print context.
	 */
	PCGEXELEMENTSCLUSTERSSKETCH_API TSharedPtr<PCGExGraphs::FGraphBuilder> PrintResolved(
		FPCGExContext* InContext,
		const FPCGExClusterSketchModel& InModel,
		const UPCGExClusterSnapProvider* InSnapProvider,
		TConstArrayView<TObjectPtr<UPCGExClusterSketchDecorator>> InDecorators,
		const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext,
		const FPCGExGraphBuilderDetails* InBuilderDetails,
		bool bQuiet = false,
		TFunction<void(const TSharedRef<PCGExGraphs::FGraphBuilder>&, bool)> OnCompiled = nullptr);

	PCGEXELEMENTSCLUSTERSSKETCH_API TSharedPtr<PCGExGraphs::FGraphBuilder> PrintClusterSketch(
		FPCGExContext* InContext,
		const FPrintRequest& InRequest,
		const TSharedPtr<PCGExData::FPointIO>& InVtxIO,
		const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager,
		const TSharedPtr<FPCGExClusterSketchPrintContext>& InPrintContext);
}
