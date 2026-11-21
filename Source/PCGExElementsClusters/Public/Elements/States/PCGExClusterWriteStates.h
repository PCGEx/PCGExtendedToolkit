// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "PCGExClusterWriteStates.generated.h"

namespace PCGExClusterStates
{
	class FStateManager;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-write-states"))
class UPCGExFlagNodesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	PCGEX_NODE_INFOS(FlagNodes, "Cluster : Write States", "Writes cluster states as a int64 flag mask");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Metadata;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterState);
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	/** Attribute to output flags to */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName FlagAttribute = FName("Flags");

	/** Initial flags */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int64 InitialFlags;

private:
	friend class FPCGExFlagNodesElement;
};

struct FPCGExFlagNodesContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExFlagNodesElement;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> StateFactories;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExFlagNodesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(FlagNodes)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExFlagNodes
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExFlagNodesContext, UPCGExFlagNodesSettings>
	{
		friend class FBatch;
		TSharedPtr<TArray<int64>> StateFlags;
		TSharedPtr<PCGExClusterStates::FStateManager> StateManager;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		TSharedPtr<TArray<int64>> StateFlags;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
			bAllowVtxDataFacadeScopedGet = true;
		}

		virtual ~FBatch() override;

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void CompleteWork() override;
	};
}
