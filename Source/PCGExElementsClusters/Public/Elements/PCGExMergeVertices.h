// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"

#include "PCGExMergeVertices.generated.h"

class FPCGExPointIOMerger;

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/utilities/cluster-merge-vtx"))
class UPCGExMergeVerticesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(MergeVertices, "Cluster : Merge Vtx", "Merge Vtx so all edges share the same vtx collection.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExClustersProcessorSettings interface
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExClustersProcessorSettings interface

	/** Meta filter settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;
};

struct FPCGExMergeVerticesContext final : FPCGExClustersProcessorContext
{
	friend class UPCGExMergeVerticesSettings;
	friend class FPCGExMergeVerticesElement;

	FPCGExCarryOverDetails CarryOverDetails;

	PCGExDataId OutVtxId;
	TSharedPtr<PCGExData::FFacade> CompositeDataFacade;
	TSharedPtr<FPCGExPointIOMerger> Merger;

	virtual void ClusterProcessing_InitialProcessingDone() override;
	virtual void ClusterProcessing_WorkComplete() override;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExMergeVerticesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(MergeVertices)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExMergeVertices
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExMergeVerticesContext, UPCGExMergeVerticesSettings>
	{
	protected:
		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;

	public:
		int32 StartIndexOffset = 0;

		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;
		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
		virtual void Write() override;
	};
}
