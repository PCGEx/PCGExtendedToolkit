// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"


#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExFilterTypeSets.h"

#include "PCGExSampleNeighbors.generated.h"

namespace PCGExNeighborSample
{
	struct FNeighbor;
}

class UPCGExNeighborSamplerFactoryData;
class FPCGExNeighborSampleOperation;

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="sampling/cluster-sample-neighbors"))
class UPCGExSampleNeighborsSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(SampleNeighbors, "Cluster : Sample Neighbors", "Sample cluster vtx' neighbors values.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(Sampling);
	}
#endif
	PCGEX_NODE_POINT_FILTER(PCGExClusters::Labels::SourceVtxFiltersLabel, "Optional per-node filters. Only nodes that pass the filter will be sampled; filtered-out nodes can still be read by other nodes.", PCGExFactories::ClusterNodeFilters(), false)

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	//~Begin UPCGExClustersProcessorSettings
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

private:
	friend class FPCGExSampleNeighborsElement;
};

struct FPCGExSampleNeighborsContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExSampleNeighborsElement;
	TArray<TObjectPtr<const UPCGExNeighborSamplerFactoryData>> SamplerFactories;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExSampleNeighborsElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(SampleNeighbors)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExSampleNeighbors
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExSampleNeighborsContext, UPCGExSampleNeighborsSettings>
	{
		TArray<TSharedPtr<FPCGExNeighborSampleOperation>> SamplingOperations;
		TArray<TSharedPtr<FPCGExNeighborSampleOperation>> OpsWithValueTest;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void OnRangeProcessingComplete() override;

		virtual void PrepareLoopScopesForNodes(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;

		virtual void Write() override;
		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
			PCGEX_TYPED_CONTEXT_AND_SETTINGS(SampleNeighbors)
			bRequiresWriteStep = true;
			bWriteVtxDataFacade = true;
			bAllowVtxDataFacadeScopedGet = true; // TODO : More work required to support this
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
	};
}
