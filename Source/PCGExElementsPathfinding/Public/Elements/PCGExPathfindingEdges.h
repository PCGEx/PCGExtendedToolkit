// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExPathfinding.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Paths/PCGExPathOutputDetails.h"
#include "PCGExPathfindingEdges.generated.h"

namespace PCGExPathfinding
{
	class FSearchAllocations;
	class FPathQuery;
}

class UPCGExSearchInstancedFactory;
/**
 * Use PCGExTransform to manipulate the outgoing attributes instead of handling everything here.
 * This way we can multi-thread the various calculations instead of mixing everything along with async/game thread collision
 */
UCLASS(BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc", meta=(PCGExNodeLibraryDoc="pathfinding/pathfinding-edges"))
class UPCGExPathfindingEdgesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PathfindingEdges, "Pathfinding : Edges", "Extract paths from edges clusters.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Pathfinding);
	}
#endif
	
	virtual bool SupportsDataStealing() const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

	//~Begin UObject interface
#if WITH_EDITOR
	virtual void PostInitProperties() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~End UObject interface

	/** Whether to output paths, or to forward Vtx & Edges with a per-element visited count instead. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExPathfindingOutputMode OutputMode = EPCGExPathfindingOutputMode::Paths;

	/** Controls how goals are picked.*/
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExGoalPicker> GoalPicker;

	/** Add seed point at the beginning of the path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	bool bAddSeedToPath = false;

	/** Add goal point at the end of the path */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	bool bAddGoalToPath = false;

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

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** Copy seed point attributes as tags on output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	FPCGExAttributeToTagDetails SeedAttributesToPathTags;

	/** Which Seed attributes to forward on paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	FPCGExForwardDetails SeedForwarding;

	/** Copy goal point attributes as tags on output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	FPCGExAttributeToTagDetails GoalAttributesToPathTags;

	/** Which Goal attributes to forward on paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging & Forwarding", meta=(EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	FPCGExForwardDetails GoalForwarding;

	/** Per-element visited counts written onto the forwarded Vtx & Edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Visited", EditCondition="OutputMode==EPCGExPathfindingOutputMode::Visited", EditConditionHides))
	FPCGExPathStatistics Statistics;

	/** ... */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Paths Output Settings", EditCondition="OutputMode==EPCGExPathfindingOutputMode::Paths", EditConditionHides))
	FPCGExPathOutputDetails PathOutputDetails;

	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or slightly slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

	/** If disabled, will share memory allocations between queries, forcing them to execute one after another. Much slower, but very conservative for memory.  Using global feedback forces this behavior under the hood.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bGreedyQueries = true;
};

struct FPCGExPathfindingEdgesContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExPathfindingEdgesElement;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
	TSharedPtr<PCGExData::FFacade> GoalsDataFacade;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;

	UPCGExGoalPicker* GoalPicker = nullptr;
	UPCGExSearchInstancedFactory* SearchAlgorithm = nullptr;

	FPCGExAttributeToTagDetails SeedAttributesToPathTags;
	FPCGExAttributeToTagDetails GoalAttributesToPathTags;

	TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;
	TSharedPtr<PCGExData::FDataForwardHandler> GoalForwardHandler;

	TArray<uint64> SeedGoalPairs;

	void BuildPath(const TSharedPtr<PCGExPathfinding::FPathQuery>& Query, const TSharedPtr<PCGExData::FPointIO>& PathIO);

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExPathfindingEdgesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PathfindingEdges)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPathfindingEdges
{
	class FBatch;

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExPathfindingEdgesContext, UPCGExPathfindingEdgesSettings>
	{
		friend class FBatch;

		TArray<TSharedPtr<PCGExPathfinding::FPathQuery>> Queries;
		TArray<TSharedPtr<PCGExData::FPointIO>> QueriesIO;
		TSharedPtr<PCGExPathfinding::FSearchAllocations> SearchAllocations;

		// Visited mode: per-element counts written via atomic increments. The vtx buffer is owned
		// by the batch (shared across the batch's clusters); the edge buffer is per-processor.
		int32* VisitedVtxData = nullptr;
		int32* VisitedEdgeData = nullptr;
		TSharedPtr<PCGExData::TBuffer<int32>> VisitedEdgeWriter;

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
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		// Visited mode: vtx counts live on the (batch-shared) vtx facade, allocated once here.
		TSharedPtr<PCGExData::TBuffer<int32>> VisitedVtxWriter;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
		}

		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
	};
}
