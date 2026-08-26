// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExEdgeRefineOperation.h"
#include "Details/PCGExFilterDetails.h"

#include "PCGExRefineEdges.generated.h"

namespace PCGExRefineEdges
{
	const FName SourceSanitizeEdgeFilters = FName("SanitizeFilters");
}

UENUM()
enum class EPCGExRefineSanitization : uint8
{
	None     = 0 UMETA(DisplayName = "None", ToolTip="No sanitization."),
	Shortest = 1 UMETA(DisplayName = "Shortest", ToolTip="If a node has no edge left, restore the shortest one."),
	Longest  = 2 UMETA(DisplayName = "Longest", ToolTip="If a node has no edge left, restore the longest one."),
	Filters  = 3 UMETA(DisplayName = "Filters", ToolTip="Use filters to find edges that must be preserved."),
};

UENUM()
enum class EPCGExRefineEdgesOutput : uint8
{
	Clusters  = 0 UMETA(DisplayName = "Clusters", ToolTip="Outputs clusters."),
	Points    = 1 UMETA(DisplayName = "Points", ToolTip="Outputs regular points (edges only)"),
	Attribute = 3 UMETA(DisplayName = "Attribute", ToolTip="Writes the result of the filters to an attribute."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(Keywords = "filter edge mst minimum spanning tree skeleton gabriel"), meta=(PCGExNodeLibraryDoc="clusters/refine/cluster-refine"))
class UPCGExRefineEdgesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(RefineEdges, "Cluster : Refine", "Refine edges according to special rules.", (Refinement ? FName(Refinement.GetClass()->GetMetaData(TEXT("DisplayName"))) : FName("...")));

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(ClusterOp);
	}
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExEdgeRefineInstancedFactory> Refinement;

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExRefineEdgesOutput Mode = EPCGExRefineEdgesOutput::Clusters;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" ├─ Vtx Result", EditCondition="Mode == EPCGExRefineEdgesOutput::Attribute", EditConditionHides))
	FPCGExFilterResultDetails ResultOutputVtx;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName=" └─ Edge Result", EditCondition="Mode == EPCGExRefineEdgesOutput::Attribute", EditConditionHides))
	FPCGExFilterResultDetails ResultOutputEdges;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	bool bOutputEdgesOnly_DEPRECATED = false;

#pragma endregion

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition = "Mode == EPCGExRefineEdgesOutput::Points", EditConditionHides))
	bool bAllowZeroPointOutputs = false;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition = "Mode == EPCGExRefineEdgesOutput::Clusters", EditConditionHides))
	EPCGExRefineSanitization Sanitization = EPCGExRefineSanitization::None;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition = "Mode == EPCGExRefineEdgesOutput::Clusters", EditConditionHides))
	bool bRestoreEdgesThatConnectToValidNodes = false;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings", EditCondition="Mode == EPCGExRefineEdgesOutput::Clusters", EditConditionHides))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

private:
	friend class FPCGExRefineEdgesElement;
};

struct FPCGExRefineEdgesContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExRefineEdgesElement;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> EdgeFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> SanitizationFilterFactories;

	UPCGExEdgeRefineInstancedFactory* Refinement = nullptr;

	TSharedPtr<PCGExData::FPointIOCollection> KeptEdges;
	TSharedPtr<PCGExData::FPointIOCollection> RemovedEdges;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExRefineEdgesElement final : public FPCGExClustersProcessorElement
{
	virtual bool CanExecuteOnlyOnMainThread(FPCGContext* InContext) const override
	{
		if (!InContext)
		{
			return true;
		}
		PCGEX_CONTEXT(RefineEdges)
		return Context->Refinement ? Context->Refinement->CanOnlyExecuteOnMainThread() : false;
	}

protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(RefineEdges)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExRefineEdges
{
	const FName SourceOverridesRefinement = TEXT("Overrides : Refinement");

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExRefineEdgesContext, UPCGExRefineEdgesSettings>
	{
		friend class FSanitizeRangeTask;
		friend class FFilterRangeTask;

	protected:
		TSharedPtr<PCGExClusterFilter::FManager> SanitizationFilterManager;
		EPCGExRefineSanitization Sanitization = EPCGExRefineSanitization::None;

		FPCGExFilterResultDetails ResultOutputVtx = FPCGExFilterResultDetails(false, false);
		FPCGExFilterResultDetails ResultOutputEdges = FPCGExFilterResultDetails(false, false);

		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;
		mutable FRWLock NodeLock;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;

		void PrepareSingleLoopScopeForEdges(const PCGExMT::FScope& Scope);
		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;
		virtual void OnEdgesProcessingComplete() override;
		void Sanitize();
		void InsertEdges() const;
		virtual void CompleteWork() override;

		TSharedPtr<FPCGExEdgeRefineOperation> Refinement;

		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	protected:
		FPCGExFilterResultDetails ResultOutputVtx;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
			PCGEX_TYPED_CONTEXT_AND_SETTINGS(RefineEdges)
			bRequiresGraphBuilder = Settings->Mode == EPCGExRefineEdgesOutput::Clusters;
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual void Write() override;
	};
}
