// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Details/PCGExBlendingDetails.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExBuildDualGraph.generated.h"

namespace PCGExBlending
{
	class FUnionBlender;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExClusters
{
	class FPlanarFaceEnumerator;
}

namespace PCGExBuildDualGraph
{
	/** Context for edge blending during subgraph compilation */
	struct FEdgeBlendContext final : PCGExGraphs::FSubGraphUserContext
	{
		TArray<int32> EdgeToSharedPoint;
		TSharedPtr<PCGExBlending::FUnionBlender> EdgeBlender;
	};
}

/**
 * Build the edge dual graph: edges become vertices, vertices become edges connecting adjacent original edges.
 * Uses DCEL half-edge traversal to connect edges that are sequential around shared vertices.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-dual-graph"))
class UPCGExBuildDualGraphSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildDualGraph, "Cluster : Edge Dual Graph", "Build the edge dual graph: edges become vertices that connect to sequential edges around shared endpoints.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_BLEND(ClusterGenerator, Pathfinding);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	/** Projection settings for DCEL construction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Graph output settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Write original edge length to new vertices */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteEdgeLength = false;

	/** Attribute name for original edge length */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteEdgeLength"))
	FName EdgeLengthAttributeName = FName("EdgeLength");

	/** Write original edge index to new vertices */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteOriginalEdgeIndex = false;

	/** Attribute name for original edge index */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Attributes", meta = (PCG_Overridable, EditCondition="bWriteOriginalEdgeIndex"))
	FName OriginalEdgeIndexAttributeName = FName("OriginalEdgeIndex");

	/** Defines how original edge attributes are blended to new vertices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending (Vtx from Edges)", meta = (PCG_Overridable))
	FPCGExBlendingDetails VtxBlendingDetails = FPCGExBlendingDetails(EPCGExBlendingType::None, EPCGExBlendingType::None);

	/** Defines how original vertex attributes are blended to new edges (from shared endpoint). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending (Edges from Vtx)", meta = (PCG_Overridable))
	FPCGExBlendingDetails EdgeBlendingDetails = FPCGExBlendingDetails(EPCGExBlendingType::None, EPCGExBlendingType::None);

	/** Meta filter settings for attribute carry-over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending (Vtx from Edges)", meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails VtxCarryOverDetails;

	/** Meta filter settings for attribute carry-over. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Blending (Edges from Vtx)", meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails EdgeCarryOverDetails;

private:
	friend class FPCGExBuildDualGraphElement;
};

struct FPCGExBuildDualGraphContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExBuildDualGraphElement;

	FPCGExCarryOverDetails VtxCarryOverDetails;
	FPCGExCarryOverDetails EdgeCarryOverDetails;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExBuildDualGraphElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildDualGraph)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildDualGraph
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExBuildDualGraphContext, UPCGExBuildDualGraphSettings>
	{
	protected:
		TSharedPtr<PCGExData::FFacade> DualVtxFacade;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;
		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> FaceEnumerator;

		int32 NumValidEdges = 0;
		TSet<uint64> DualEdgeHashes;

		// Maps dual edge hash to shared node's point index (for edge blending)
		TMap<uint64, int32> DualEdgeToSharedPointIdx;

		// Blender for edge attributes → dual vertex attributes
		TSharedPtr<PCGExBlending::FUnionBlender> VtxBlender;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;
	};
}
