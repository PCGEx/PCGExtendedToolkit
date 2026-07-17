// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExPathfinding.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExMatchingDetails.h"
#include "Paths/PCGExPathOutputDetails.h"

#include "PCGExPathfindingPlotEdges.generated.h"

namespace PCGExPathfinding
{
	class FSearchAllocations;
	class FPlotQuery;
}

namespace PCGExClusters
{
	class FClusterDataForwardHandler;
}

namespace PCGExData
{
	class FDataForwardHandler;
}

namespace PCGExMatching
{
	class FTargetsHandler;
	class FDataMatcher;
}

namespace PCGExSampling
{
	class FTargetsHandler;
}

class FPCGExHeuristicOperation;
class UPCGExSearchInstancedFactory;
/**
 * Use PCGExTransform to manipulate the outgoing attributes instead of handling everything here.
 * This way we can multi-thread the various calculations instead of mixing everything along with async/game thread collision
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="pathfinding/pathfinding-plot-edges"))
class UPCGExPathfindingPlotEdgesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(PathfindingPlotEdges, "Pathfinding : Plot Edges", "Extract a single path from edges clusters, going through every seed points in order.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Pathfinding);
	}
#endif

protected:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UObject interface
public:
	
	virtual bool SupportsDataStealing() const override;
	
#if WITH_EDITOR
	virtual void PostInitProperties() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	/** Whether to output paths, or to forward Vtx & Edges with a per-element visited count instead. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExPathfindingOutputMode OutputMode = EPCGExPathfindingOutputMode::Paths;

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** If enabled, allows you to filter out which plots get associated to which clusters */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	FPCGExMatchingDetails DataMatching = FPCGExMatchingDetails(EPCGExMatchingDetailsUsage::Cluster);

	/** Add seed point at the beginning of the path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	bool bAddSeedToPath = false;

	/** Add goal point at the beginning of the path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	bool bAddGoalToPath = false;

	/** Insert plot points inside the path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	bool bAddPlotPointsToPath = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bClosedLoop = false;

	/** What are the paths made of. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	EPCGExPathComposition PathComposition = EPCGExPathComposition::Vtx;

	/** Drive how a seed selects a node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Node Picking", meta=(PCG_Overridable))
	FPCGExNodeSelectionDetails SeedPicking;

	/** Drive how a goal selects a node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Node Picking", meta=(PCG_Overridable))
	FPCGExNodeSelectionDetails GoalPicking;

	/** Search algorithm. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExSearchInstancedFactory> SearchAlgorithm;

	/** Per-element visited counts written onto the forwarded Vtx & Edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Visited", EditCondition="OutputMode==EPCGExPathfindingOutputMode::Visited", EditConditionHides))
	FPCGExPathStatistics Statistics;

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	bool bOmitCompletePathOnFailedPlot = false;

	/** ... */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Paths Output Settings", EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	FPCGExPathOutputDetails PathOutputDetails;

	/** Which data is forwarded from plots to paths */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding")
	FPCGExForwardDetails PlotForwarding;

	/** Which data is forwarded from vtx to paths. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="PathComposition==EPCGExPathComposition::Vtx"))
	FPCGExForwardDetails VtxDataForwarding;

	/** Which data is forwarded from edges to paths. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="PathComposition==EPCGExPathComposition::Edges"))
	FPCGExForwardDetails EdgesDataForwarding;

	/** Suppress warning when a plot could not be resolved to valid paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietInvalidPlotWarning = false;

	/** If disabled, will share memory allocations between queries, forcing them to execute one after another. Much slower, but very conservative for memory. Using global feedback forces this behavior under the hood. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bGreedyQueries = true;
};


struct FPCGExPathfindingPlotEdgesContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExPathfindingPlotEdgesElement;

	TSharedPtr<PCGExMatching::FTargetsHandler> PlotsHandler;
	TSharedPtr<PCGExMatching::FDataMatcher> MainDataMatcher;
	TSharedPtr<PCGExMatching::FDataMatcher> EdgeDataMatcher;
	int32 NumMaxPlots = 0;

	bool bMatchForVtx = false;
	bool bMatchForEdges = false;

	TMap<int32, TSharedPtr<PCGExData::FDataForwardHandler>> PlotsForwardHandlers;
	FPCGExForwardDetails VtxDataForwarding;
	FPCGExForwardDetails EdgesDataForwarding;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;

	UPCGExSearchInstancedFactory* SearchAlgorithm = nullptr;

	void BuildPath(const TSharedPtr<PCGExPathfinding::FPlotQuery>& Query, const TSharedPtr<PCGExData::FPointIO>& PathIO, const TSharedPtr<PCGExClusters::FClusterDataForwardHandler>& ClusterForwardHandler = nullptr) const;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExPathfindingPlotEdgesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathfindingPlotEdges)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathfindingPlotEdges
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExPathfindingPlotEdgesContext, UPCGExPathfindingPlotEdgesSettings>
	{
		friend class FBatch;

	protected:
		TSet<const UPCGData*>* VtxIgnoreList = nullptr;
		TSet<const UPCGData*> IgnoreList;
		TArray<TSharedPtr<PCGExData::FFacade>> ValidPlots;
		TArray<TSharedPtr<PCGExPathfinding::FPlotQuery>> Queries;
		TArray<TSharedPtr<PCGExData::FPointIO>> QueriesIO;
		TSharedPtr<PCGExPathfinding::FSearchAllocations> SearchAllocations;

		TSharedPtr<PCGExClusters::FClusterDataForwardHandler> ClusterDataForwardHandler;

		// Visited mode: per-element counts written via atomic increments. The vtx buffer is owned
		// by the batch (shared across the batch's clusters); the edge buffer is per-processor.
		int32* VisitedVtxData = nullptr;
		int32* VisitedEdgeData = nullptr;
		TSharedPtr<PCGExData::TBuffer<int32>> VisitedEdgeWriter;

		// Handles one finished plot: marks visited counts (Visited mode) or builds the path.
		void OnPlotComplete(const TSharedPtr<PCGExPathfinding::FPlotQuery>& Plot);

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		TSharedPtr<FPCGExSearchOperation> SearchOperation;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void Write() override;
		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	protected:
		bool bUnmatched = false;
		TSet<const UPCGData*> IgnoreList;
		TSharedPtr<PCGExData::FDataForwardHandler> VtxDataForwardHandler;

		// Visited mode: vtx counts live on the (batch-shared) vtx facade, allocated once here.
		TSharedPtr<PCGExData::TBuffer<int32>> VisitedVtxWriter;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
		}

		virtual void Process() override;
		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void CompleteWork() override;
	};
}
