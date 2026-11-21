// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Details/PCGExFilterDetails.h"

#include "PCGExFilterVtx.generated.h"

UENUM()
enum class EPCGExVtxFilterOutput : uint8
{
	Clusters  = 0 UMETA(DisplayName = "Clusters", ToolTip="Outputs clusters."),
	Points    = 1 UMETA(DisplayName = "Points", ToolTip="Outputs regular points"),
	Attribute = 3 UMETA(DisplayName = "Attribute", ToolTip="Writes the result of the filters to a boolean attribute."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/refine/cluster-filter-vtx"))
class UPCGExFilterVtxSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;

	PCGEX_NODE_INFOS(FilterVtx, "Cluster : Filter Vtx", "Filter out vtx from clusters.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Filter;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(ClusterOp);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return Mode == EPCGExVtxFilterOutput::Attribute;
	}

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	/** Type of output */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExVtxFilterOutput Mode = EPCGExVtxFilterOutput::Clusters;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName=" └─ Result", PCG_Overridable, EditCondition="Mode == EPCGExVtxFilterOutput::Attribute", EditConditionHides))
	FPCGExFilterResultDetails ResultOutputVtx;

	/** If enabled, invalidating a node invalidate connected edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bNodeInvalidateEdges = false;

	/** Invert the filter result */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	/** Invert the edge filters result */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExVtxFilterOutput::Clusters", EditConditionHides))
	bool bInvertEdgeFilters = false;

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName ResultAttributeName_DEPRECATED = NAME_None;

#pragma endregion

	/** If enabled, inside/outside groups will be partitioned by initial edge connectivity. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable, EditCondition="Mode == EPCGExVtxFilterOutput::Points", EditConditionHides))
	bool bSplitOutputsByConnectivity = true;

	/** Swap Inside & Outside content */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Mode == EPCGExVtxFilterOutput::Points", EditConditionHides))
	bool bSwap = false;

	/** Tag clusters where at least one vertex passed the filter. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfAnyPointPassed = false;

	/** Tag to apply when some points passed. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfAnyPointPassed"))
	FString HasAnyPointPassedTag = TEXT("SomePointsPassed");

	/** Tag clusters where all vertices passed the filter. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfAllPointsPassed = false;

	/** Tag to apply when all points passed. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfAllPointsPassed"))
	FString AllPointsPassedTag = TEXT("AllPointsPassed");

	/** Tag clusters where no vertices passed the filter. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(InlineEditConditionToggle))
	bool bTagIfNoPointPassed = false;

	/** Tag to apply when no points passed. */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(EditCondition="bTagIfNoPointPassed"))
	FString NoPointPassedTag = TEXT("NoPointPassed");


	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings", EditCondition="Mode == EPCGExVtxFilterOutput::Clusters", EditConditionHides))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

private:
	friend class FPCGExFilterVtxElement;
};

struct FPCGExFilterVtxContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFilterVtxElement;

	bool bWantsClusters = true;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> EdgeFilterFactories;

	TSharedPtr<PCGExData::FPointIOCollection> Inside;
	TSharedPtr<PCGExData::FPointIOCollection> Outside;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFilterVtxElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FilterVtx)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFilterVtx
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFilterVtxContext, UPCGExFilterVtxSettings>
	{
		friend class FBatch;

	protected:
		FPCGExFilterResultDetails ResultOutputVtx = FPCGExFilterResultDetails(false, false);

		int32 PassNum = 0;
		int32 FailNum = 0;

		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;

		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	protected:
		FPCGExFilterResultDetails ResultOutputVtx = FPCGExFilterResultDetails(false, false);

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
			PCGEX_TYPED_CONTEXT_AND_SETTINGS(FilterVtx)

			bRequiresGraphBuilder = Settings->Mode == EPCGExVtxFilterOutput::Clusters;
			bRequiresWriteStep = Settings->Mode == EPCGExVtxFilterOutput::Attribute;
		}

		virtual void OnProcessingPreparationComplete() override;

		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
