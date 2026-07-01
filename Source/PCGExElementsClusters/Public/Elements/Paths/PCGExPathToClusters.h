// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPathProcessor.h"
#include "Core/PCGExUnionRegistry.h"
#include "Core/PCGExUnionTable.h"
#include "Graphs/Union/PCGExIntersections.h"
#include "Graphs/Union/PCGExUnionProcessor.h"
#include "PCGExPathToClusters.generated.h"

namespace PCGExPathToClusters
{
	class FFusingProcessor;

	// Same shape as PCGExFuseClusters::FStagedEdge but locally namespaced -- paths use IO=-1 / Index=-1
	// in the abstract field (no edge point data). Resolved into the central edge table after node compile.
	struct FStagedEdge
	{
		uint64 KeyA = 0;
		uint64 KeyB = 0;

		FStagedEdge() = default;

		FStagedEdge(const uint64 InKeyA, const uint64 InKeyB)
			: KeyA(InKeyA)
			  , KeyB(InKeyB)
		{
		}
	};
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="clusters/interop/path-to-clusters"))
class UPCGExPathToClustersSettings : public UPCGExPathProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathsToClusters, "Path : To Clusters", "Merge paths to edge clusters for glorious pathfinding inception");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	//~End UPCGExPointsProcessorSettings

	/** Whether to fuse paths into a single graph or not. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bFusePaths = true;

	/** Fuse Settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing", meta=(PCG_Overridable, DisplayName="Point/Point Settings"))
	FPCGExPointPointIntersectionDetails PointPointIntersectionDetails;

	/** Find Point-Edge intersection (points on edges)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFindPointEdgeIntersections;

	/** Point-Edge intersection settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing", meta=(PCG_Overridable, DisplayName="Point/Edge Settings", EditCondition="bFindPointEdgeIntersections"))
	FPCGExPointEdgeIntersectionDetails PointEdgeIntersectionDetails;

	/** Find Edge-Edge intersection (edge crossings)*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bFindEdgeEdgeIntersections;

	/** Edge-Edge intersection settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing", meta=(PCG_Overridable, DisplayName="Edge/Edge Settings", EditCondition="bFindEdgeEdgeIntersections"))
	FPCGExEdgeEdgeIntersectionDetails EdgeEdgeIntersectionDetails;

	/** Defines how fused point properties and attributes are merged together for fused points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing|Data Blending")
	FPCGExBlendingDetails DefaultPointsBlendingDetails;

	/** Defines how fused point properties and attributes are merged together for fused edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing|Data Blending")
	FPCGExBlendingDetails DefaultEdgesBlendingDetails;

	/** Use custom blending settings for Point/Edge intersections instead of the default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing|Data Blending", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseCustomPointEdgeBlending = false;

	/** Defines how fused point properties and attributes are merged together for Point/Edge intersections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing|Data Blending", meta=(PCG_Overridable, EditCondition="bUseCustomPointEdgeBlending"))
	FPCGExBlendingDetails CustomPointEdgeBlendingDetails;

	/** Use custom blending settings for Edge/Edge intersections (crossings) instead of the default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing|Data Blending", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bUseCustomEdgeEdgeBlending = false;

	/** Defines how fused point properties and attributes are merged together for Edge/Edge intersections (Crossings). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Fusing|Data Blending", meta=(PCG_Overridable, EditCondition="bUseCustomEdgeEdgeBlending"))
	FPCGExBlendingDetails CustomEdgeEdgeBlendingDetails;

	/** Meta filter settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails(EPCGExMinimalAxis::X);
};

struct FPCGExPathToClustersContext final : FPCGExPathProcessorContext
{
	friend class FPCGExPathToClustersElement;
	friend class PCGExPathToClusters::FFusingProcessor;

	TArray<TSharedRef<PCGExData::FFacade>> PathsFacades;

	FPCGExCarryOverDetails CarryOverDetails;

	TSharedPtr<PCGExData::FFacade> UnionDataFacade;

	// Phase 1+2 streaming build state -- mirrors FuseClusters context. Edges are abstract (no
	// per-edge point data), so the EdgeBuilder receives placeholder records with IO=-1, Index=0.
	TSharedPtr<PCGExData::FUnionTableBuilder> NodeBuilder;
	TSharedPtr<PCGExData::FUnionTableBuilder> EdgeBuilder;
	TSharedPtr<PCGExData::FUnionRegistry> NodeRegistry;
	FPCGExFuseDetails FuseDetails;
	FBox FuseBounds = FBox(ForceInit);
	bool bUseOctreeMode = false;

	TSharedPtr<PCGExGraphs::FUnionProcessor> UnionProcessor;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExPathToClustersElement final : public FPCGExPathProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathToClusters)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathToClusters
{
#pragma region NonFusing

	class FNonFusingProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathToClustersContext, UPCGExPathToClustersSettings>
	{
		bool bClosedLoop = false;

	public:
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		explicit FNonFusingProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FNonFusingProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};

#pragma endregion

#pragma region Fusing
	// Fusing processors

	// TODO : Batch-preload point attributes we'll want to blend

	class FFusingProcessor final : public PCGExPointsMT::TProcessor<FPCGExPathToClustersContext, UPCGExPathToClustersSettings>
	{
		bool bClosedLoop = false;

		int32 IOIndex = 0;
		int32 LastIndex = 0;

	public:
		// Per-processor buffers populated during Process(), drained serially in the post-batch step.
		TArray<PCGExData::FUnionStreamRecord> NodeRecords;
		TArray<FStagedEdge> StagedEdges;

		explicit FFusingProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FFusingProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		void EmitEdges(const PCGExMT::FScope& Scope);
	};

#pragma endregion
}
