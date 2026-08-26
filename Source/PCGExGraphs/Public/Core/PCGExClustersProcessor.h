// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExClusterMT.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Sorting/PCGExSortingDetails.h"

#include "PCGExClustersProcessor.generated.h"

#define PCGEX_ELEMENT_BATCH_EDGE_DECL virtual TSharedPtr<PCGExClusterMT::IBatch> CreateEdgeBatchInstance(const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges) const override;
#define PCGEX_ELEMENT_BATCH_EDGE_IMPL(_CLASS) TSharedPtr<PCGExClusterMT::IBatch> FPCGEx##_CLASS##Context::CreateEdgeBatchInstance(const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges) const{ \
return MakeShared<PCGExClusterMT::TBatch<PCGEx##_CLASS::FProcessor>>(const_cast<FPCGEx##_CLASS##Context*>(this), InVtx, InEdges); }
#define PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(_CLASS) TSharedPtr<PCGExClusterMT::IBatch> FPCGEx##_CLASS##Context::CreateEdgeBatchInstance(const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges) const{ \
return MakeShared<PCGEx##_CLASS::FBatch>(const_cast<FPCGEx##_CLASS##Context*>(this), InVtx, InEdges); }

#define PCGEX_CLUSTER_BATCH_PROCESSING(_STATE) if (!Context->ProcessClusters(_STATE)) { return false; }

namespace PCGExClusters
{
	class FDataLibrary;
}

namespace PCGExData
{
	class FPointIOTaggedEntries;
}

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), meta=(PCGExNodeLibraryDoc="clusters/common-settings/cluster-processor-settings"))
class PCGEXGRAPHS_API UPCGExClustersProcessorSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ClustersProcessorSettings, "Edges Processor Settings", "Processes vtx/edges cluster pairs.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterOp);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual bool SupportsEdgeSorting() const;
	virtual bool RequiresEdgeSorting() const;

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const;

	virtual FName GetMainInputPin() const override
	{
		return PCGExClusters::Labels::SourceVerticesLabel;
	}

	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	virtual bool GetMainAcceptMultipleData() const override;
	//~End UPCGExPointsProcessorSettings

	/** Whether endpoint index lookups are built per-scope rather than upfront. Default follows the plugin's Performance settings (disabled unless changed there). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	EPCGExOptionState ScopedIndexLookupBuild = EPCGExOptionState::Default;

	/** Suppress warning when a cluster pair (vtx/edges) cannot be found. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bQuietMissingClusterPairElement = false;

	bool WantsScopedIndexLookupBuild() const;
};

struct PCGEXGRAPHS_API FPCGExClustersProcessorContext : FPCGExPointsProcessorContext
{
	friend class UPCGExClustersProcessorSettings;
	friend class FPCGExClustersProcessorElement;

	using FBatchProcessingValidateEntries = std::function<bool(const TSharedPtr<PCGExData::FPointIOTaggedEntries>&)>;
	using FBatchProcessingInitEdgeBatch = std::function<void(const TSharedPtr<PCGExClusterMT::IBatch>&)>;

	virtual ~FPCGExClustersProcessorContext() override;


	TSharedPtr<PCGExData::FPointIOCollection> MainEdges;
	TSharedPtr<PCGExClusters::FDataLibrary> ClusterDataLibrary;
	TSharedPtr<PCGExData::FPointIOTaggedEntries> TaggedEdges;

	const TArray<FPCGExSortRuleConfig>* GetEdgeSortingRules() const;

	virtual bool AdvancePointsIO(const bool bCleanupKeys = true) override;

	TSharedPtr<PCGExClusters::FCluster> CurrentCluster;

	void OutputPointsAndEdges() const;

	FPCGExGraphBuilderDetails GraphBuilderDetails;

	int32 GetClusterProcessorsNum() const;

	template <typename T>
	void GatherClusterProcessors(TArray<TSharedPtr<T>>& OutProcessors)
	{
		OutProcessors.Reserve(GetClusterProcessorsNum());
		for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
		{
			const int32 NumProcessors = Batch->GetNumProcessors();
			OutProcessors.Reserve(OutProcessors.Num() + NumProcessors);
			for (int i = 0; i < NumProcessors; i++)
			{
				OutProcessors.Add(Batch->GetProcessor<T>(i));
			}
		}
	}

	void OutputBatches() const;

protected:
	virtual TSharedPtr<PCGExClusterMT::IBatch> CreateEdgeBatchInstance(const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges) const;
	TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> HeuristicsFactories;

public:
	bool ProcessClusters(const PCGExCommon::ContextState NextStateId);

	bool GetHasValidHeuristics() const
	{
		return bHasValidHeuristics;
	}

protected:
	bool CompileGraphBuilders(const bool bOutputToContext, const PCGExCommon::ContextState NextStateId);

	TArray<FPCGExSortRuleConfig> EdgeSortingRules;

	TArray<TSharedPtr<PCGExClusterMT::IBatch>> Batches;
	TArray<TSharedRef<PCGExData::FFacade>> EdgesDataFacades;

	bool bScopedIndexLookupBuild = false;
	bool bHasValidHeuristics = false;

	bool bSkipClusterBatchCompletionStep = false;
	bool bDoClusterBatchWritingStep = false;
	bool bDaisyChainClusterBatches = false;

	int32 CurrentBatchIndex = -1;
	TSharedPtr<PCGExClusterMT::IBatch> CurrentBatch;

	bool StartProcessingClusters(FBatchProcessingValidateEntries&& ValidateEntries, FBatchProcessingInitEdgeBatch&& InitBatch, const bool bDaisyChain = false);

	virtual void ClusterProcessing_InitialProcessingDone();
	virtual void ClusterProcessing_WorkComplete();
	virtual void ClusterProcessing_WritingDone();
	virtual void ClusterProcessing_GraphCompilationDone();

	void AdvanceBatch(const PCGExCommon::ContextState NextStateId);

	int32 CurrentEdgesIndex = -1;

	virtual TSharedPtr<PCGExPointsMT::IBatch> CreateEdgeBatchInstance(const TArray<TWeakPtr<PCGExData::FPointIO>>& InData) const PCGEX_NOT_IMPLEMENTED_RET(CreatePointBatchInstance, nullptr);
};

class PCGEXGRAPHS_API FPCGExClustersProcessorElement : public FPCGExPointsProcessorElement
{
	
protected:
	
	virtual void DisabledPassThroughData(FPCGContext* Context) const override;
	
	PCGEX_ELEMENT_CREATE_CONTEXT(ClustersProcessor)
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void InitializeData(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
	virtual void OnContextInitialized(FPCGExContext* InContext) const override;
};
