// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Blenders/PCGExUnionBlender.h"
#include "Core/PCGExClustersProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Details/PCGExIntersectionDetails.h"

#include "Core/PCGExUnionRegistry.h"
#include "Core/PCGExUnionTable.h"

#include "PCGExFuseClusters.generated.h"

namespace PCGExGraphs
{
	class FUnionProcessor;
}

namespace PCGExFuseClusters
{
	class FProcessor;

	// Intermediate per-processor record produced during the parallel cluster scan; resolved into
	// final edge-table entries during the sequential post-batch step (after node table compile).
	struct FStagedEdge
	{
		uint64 KeyA = 0;
		uint64 KeyB = 0;
		int32 IO = -1;
		int32 EdgePointIndex = -1;

		FStagedEdge() = default;

		FStagedEdge(const uint64 InKeyA, const uint64 InKeyB, const int32 InIO, const int32 InEdgePointIndex)
			: KeyA(InKeyA)
			  , KeyB(InKeyB)
			  , IO(InIO)
			  , EdgePointIndex(InEdgePointIndex)
		{
		}
	};
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/refine/cluster-fuse"))
class UPCGExFuseClustersSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(FuseClusters, "Cluster : Fuse", "Finds Point/Edge and Edge/Edge intersections between all input clusters.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExClustersProcessorSettings interface
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExClustersProcessorSettings interface

	/** Fuse Settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Point/Point Settings"))
	FPCGExPointPointIntersectionDetails PointPointIntersectionDetails;

	/** Find Point-Edge intersection */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFindPointEdgeIntersections;

	/** Point-Edge intersection settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Point/Edge Settings", EditCondition="bFindPointEdgeIntersections"))
	FPCGExPointEdgeIntersectionDetails PointEdgeIntersectionDetails;

	/** Find Edge-Edge intersection */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFindEdgeEdgeIntersections;

	/** Edge-Edge intersection */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Edge/Edge Settings", EditCondition="bFindEdgeEdgeIntersections"))
	FPCGExEdgeEdgeIntersectionDetails EdgeEdgeIntersectionDetails;

	/** Defines how fused point properties and attributes are merged together for fused points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Data Blending", meta=(PCG_Overridable))
	FPCGExBlendingDetails DefaultPointsBlendingDetails;

	/** Defines how fused point properties and attributes are merged together for fused edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Data Blending", meta=(PCG_Overridable))
	FPCGExBlendingDetails DefaultEdgesBlendingDetails;

	/** Use separate blending settings for Point/Edge intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Data Blending", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseCustomPointEdgeBlending = false;

	/** Defines how fused point properties and attributes are merged together for Point/Edge intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Data Blending", meta=(PCG_Overridable, EditCondition="bUseCustomPointEdgeBlending"))
	FPCGExBlendingDetails CustomPointEdgeBlendingDetails;

	/** Use separate blending settings for Edge/Edge intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Data Blending", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseCustomEdgeEdgeBlending = false;

	/** Defines how fused point properties and attributes are merged together for Edge/Edge intersections (Crossings). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Data Blending", meta=(PCG_Overridable, EditCondition="bUseCustomEdgeEdgeBlending"))
	FPCGExBlendingDetails CustomEdgeEdgeBlendingDetails;


	/** Meta filter settings for Vtx. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Meta Filters", meta = (PCG_Overridable, DisplayName="Carry Over Settings - Vtx"))
	FPCGExCarryOverDetails VtxCarryOverDetails;

	/** Meta filter settings for Edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Meta Filters", meta = (PCG_Overridable, DisplayName="Carry Over Settings - Edges"))
	FPCGExCarryOverDetails EdgesCarryOverDetails;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;
};

struct FPCGExFuseClustersContext final : FPCGExClustersProcessorContext
{
	friend class UPCGExFuseClustersSettings;
	friend class FPCGExFuseClustersElement;
	friend class PCGExFuseClusters::FProcessor;

	TArray<TSharedRef<PCGExData::FFacade>> VtxFacades;
	TSharedPtr<PCGExData::FFacade> UnionDataFacade;

	// Phase 1+2 streaming build state. NodeBuilder collects (GridKey, IO, PtIndex) records as
	// each cluster's edges are scanned; NodeRegistry handles the octree-fuse path which is
	// sequential by contract (FindOrInsert is single-threaded). EdgeBuilder is fed in the
	// post-batch sequential step once the node-key → node-index mapping is known.
	TSharedPtr<PCGExData::FUnionTableBuilder> NodeBuilder;
	TSharedPtr<PCGExData::FUnionTableBuilder> EdgeBuilder;
	TSharedPtr<PCGExData::FUnionRegistry> NodeRegistry;
	FPCGExFuseDetails FuseDetails;
	FBox FuseBounds = FBox(ForceInit);
	bool bUseOctreeMode = false;

	FPCGExCarryOverDetails VtxCarryOverDetails;
	FPCGExCarryOverDetails EdgesCarryOverDetails;

	TSharedPtr<PCGExGraphs::FUnionProcessor> UnionProcessor;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFuseClustersElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FuseClusters)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFuseClusters
{
	// TODO : Batch-preload vtx & edges attributes we'll want to blend
	// We'll need a custom FBatch to handle it

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFuseClustersContext, UPCGExFuseClustersSettings>
	{
		TArray<PCGExGraphs::FEdge> IndexedEdges;

	public:
		bool bInvalidEdges = true;

		// Per-processor buffers populated during Process(); collected serially in the
		// post-batch sequential phase so the central builders see input in deterministic order.
		TArray<PCGExData::FUnionStreamRecord> NodeRecords;
		TArray<FStagedEdge> StagedEdges;

		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
			bBuildCluster = false;
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void EmitEdges(const PCGExMT::FScope& Scope);
	};
}
