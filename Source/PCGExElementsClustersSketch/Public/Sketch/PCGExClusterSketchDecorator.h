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
 * sketch; runs during PCG execution, possibly off the game thread.
 *
 * Contract: const + stateless -- read only your own UPROPERTYs and the print context; every asset you
 * need must be declared through CollectAssetDependencies and loaded by the caller up front. A decorator
 * that needs mutable working state must instead be duplicated per-execution by its element.
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
	 */
	virtual void DecorateVertices(FPCGExClusterSketchPrintContext& Ctx) const
	{
	}

	/**
	 * Edge-domain pass. Runs inside each subgraph's pre-compile hook: writer index i is the OUTPUT edge
	 * point index; resolve the model edge as Ctx.ParentToModelEdge[Data.EdgeKeys[i].Index].
	 */
	virtual void DecorateEdges(FPCGExClusterSketchPrintContext& Ctx, const PCGExGraphs::FSubGraphPreCompileData& Data) const
	{
	}
};
