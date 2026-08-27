// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExEdgeDirectionDetails.h"

#include "PCGExFloodFillEdgeDirection.generated.h"

struct FPCGExContext;
struct FPCGExSortRuleConfig;

namespace PCGExClusters
{
	class FCluster;
}

namespace PCGExData
{
	enum class EIOInit : uint8;

	class FFacade;
	class FFacadePreloader;

	template <typename T>
	class TBuffer;
}

/**
 * Optional per-edge direction output shared by cluster propagation nodes (BFS Depth, Flood Fill).
 * Every edge whose endpoints have *different* visit depths is oriented from the shallower endpoint
 * to the deeper one (smallest -> highest depth, i.e. away from the seed). Every other edge -- equal
 * depth, one endpoint unvisited, or fully outside the visited region -- falls back to the standard
 * Edge Direction Settings (the same logic as 'Cluster : Edge Properties'). bInvertDirection negates
 * the final written vector for all edges.
 */
USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="clusters/common-settings/flood-fill-edge-direction-details"))
struct PCGEXELEMENTSFLOODFILL_API FPCGExFloodFillEdgeDirectionDetails
{
	GENERATED_BODY()

	FPCGExFloodFillEdgeDirectionDetails() = default;

	/** Write the traversal direction onto edges (depth order where it exists, Edge Direction Settings elsewhere). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bOutputDirection = false;

	/** Name of the 'FVector' attribute to write the direction to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Direction", PCG_Overridable, EditCondition="bOutputDirection"))
	FName DirectionAttributeName = FName("PropagationDirection");

	/** Invert the written direction (multiplied by -1) for every edge. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOutputDirection"))
	bool bInvertDirection = false;

	/** Fallback orientation for edges with no depth difference (equal-depth, frontier or unvisited edges). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOutputDirection"))
	FPCGExEdgeDirectionSettings DirectionSettings;

	FORCEINLINE bool WantsDirection() const { return bOutputDirection; }

	/** Whether a writer was successfully created -- use to branch once outside hot loops. */
	FORCEINLINE bool IsActive() const { return WritePtr != nullptr; }

	/** Whether the fallback needs the edge-sorting input pin (drives SupportsEdgeSorting). */
	FORCEINLINE bool RequiresSortingRules() const { return bOutputDirection && DirectionSettings.RequiresSortingRules(); }

	/** Resolve the edge output init mode: writable (Forward when stealing, else Duplicate) only when enabled. */
	PCGExData::EIOInit ResolveEdgeInitMode(bool bWantsStealing) const;

	/** Boot-time soft validation: warns and disables the output if the attribute name is invalid. */
	void ValidateNames(FPCGExContext* InContext);

	/** Batch-time: register fallback buffer dependencies (no-op when disabled). */
	void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const;

	/** Batch-time: build the shared direction sorter from the vtx facade. Returns false on hard failure. */
	bool InitForBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TArray<FPCGExSortRuleConfig>* InSortingRules);

	/** Processor-time: create the edge writer and wire the per-edge fallback reader from the batch settings. */
	bool InitForProcessor(FPCGExContext* InContext, const FPCGExFloodFillEdgeDirectionDetails& InParent, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade);

	/**
	 * Single pass over all cluster edges. Edges whose endpoints differ in depth are oriented shallow -> deep;
	 * all others fall back to DirectionSettings. InNodeDepths is indexed by node index (< 0 means "not visited").
	 * Only call when IsActive().
	 */
	void WriteFromNodeDepths(const PCGExClusters::FCluster* InCluster, const TArray<int32>& InNodeDepths) const;

protected:
	/** Write one edge, applying the invert toggle. */
	FORCEINLINE void Set(const int32 EdgePointIndex, const FVector& InDirection) const
	{
		WritePtr[EdgePointIndex] = bInvertDirection ? -InDirection : InDirection;
	}

	// Buffer keeps the output alive; WritePtr is the cached raw output array for branch-free writes.
	TSharedPtr<PCGExData::TBuffer<FVector>> DirectionWriter;
	FVector* WritePtr = nullptr;
};
