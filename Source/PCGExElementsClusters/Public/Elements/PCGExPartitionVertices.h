// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"

#include "PCGExPartitionVertices.generated.h"


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-partition-vtx"))
class UPCGExPartitionVerticesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PartitionVertices, "Cluster : Partition Vtx", "Split Vtx into per-cluster groups.");
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExClustersProcessorSettings interface
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExClustersProcessorSettings interface
};

struct FPCGExPartitionVerticesContext final : FPCGExClustersProcessorContext
{
	friend class UPCGExPartitionVerticesSettings;
	friend class FPCGExPartitionVerticesElement;

	TSharedPtr<PCGExData::FPointIOCollection> VtxPartitions;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExPartitionVerticesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PartitionVertices)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExPartitionVertices
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExPartitionVerticesContext, UPCGExPartitionVerticesSettings>
	{
	protected:
		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;

		TSharedPtr<PCGExData::FPointIO> PointPartitionIO;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};
}
