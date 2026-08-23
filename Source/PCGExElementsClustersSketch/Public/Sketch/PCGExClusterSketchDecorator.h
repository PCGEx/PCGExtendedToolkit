// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExClusterSketchDecorator.generated.h"

struct FPCGExClusterSketchPrintContext;

namespace PCGExGraphs
{
	struct FSubGraphPreCompileData;
}

/**
 * Print-time hook: adds ATTRIBUTES to the printed cluster, never topology. Lives instanced on the
 * sketch; runs during PCG execution, off the game thread (see the per-pass threading notes below).
 *
 * Contract: const + stateless -- read only your own UPROPERTYs and the print context; every asset you
 * need must be declared through CollectAssetDependencies and loaded by the caller up front. A decorator
 * that needs mutable working state must instead be duplicated per-execution by its element.
 *
 * Writes must be SYNCHRONOUS: the print's commits are instants, not barriers, so a write deferred to a
 * task is dropped. Authored values are read through Ctx.VertexDataProvider / Ctx.EdgeDataProvider,
 * indexed by model item index; both are always built, and resolve to schema defaults when nothing is
 * annotated.
 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced, CollapseCategories)
class PCGEXELEMENTSCLUSTERSSKETCH_API UPCGExClusterSketchDecorator : public UObject
{
	GENERATED_BODY()

public:
	/** Disabled decorators are skipped by the print pipeline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	bool bEnabled = true;

	/** Soft references this decorator reads during the print; loaded by the caller before printing. */
	virtual void CollectAssetDependencies(TArray<FSoftObjectPath>& OutPaths) const
	{
	}

	/**
	 * Vertex-domain pass. Runs BEFORE the vtx facade commit, so write through Ctx.VtxFacade by MODEL
	 * vertex index (point index == model index at this stage; committed values ride the compile reorder).
	 *
	 * Off the game thread, but SERIAL -- one decorator, one print at a time. The ONLY place vertex
	 * output may be written: Ctx.VtxFacade is committed the moment this pass returns.
	 */
	virtual void DecorateVertices(FPCGExClusterSketchPrintContext& Ctx) const
	{
	}

	/**
	 * Edge-domain pass. Runs inside each subgraph's pre-compile hook: writer index i is the OUTPUT edge
	 * point index; resolve the model edge as Ctx.ParentToModelEdge[Data.EdgeKeys[i].Index].
	 *
	 * Off the game thread AND concurrent across subgraphs on the SAME instance -- hence the const +
	 * stateless contract above. Ctx is shared by all of them, hence const.
	 */
	virtual void DecorateEdges(const FPCGExClusterSketchPrintContext& Ctx, const PCGExGraphs::FSubGraphPreCompileData& Data) const
	{
	}
};
