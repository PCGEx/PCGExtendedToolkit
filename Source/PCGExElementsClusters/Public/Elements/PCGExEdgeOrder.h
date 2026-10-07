// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "PCGExEdgeOrder.generated.h"

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

UENUM()
enum class EPCGExEdgeOrderMode : uint8
{
	DirectionSettings = 0 UMETA(DisplayName = "Direction Settings", ToolTip="Use per-edge direction rules."),
	DFS               = 1 UMETA(DisplayName = "DFS from Seeds", ToolTip="Orient edges by a depth-first traversal starting from external seed points. Edges are directed away from seeds (parent to child)."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-edge-order"))
class UPCGExEdgeOrderSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(EdgeOrder, "Cluster : Edge Order", "Fix an order for edge start & end endpoints.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif

	virtual bool SupportsEdgeSorting() const override
	{
		return Mode == EPCGExEdgeOrderMode::DirectionSettings && DirectionSettings.RequiresSortingRules();
	}

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

protected:
	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** How edge endpoint order is determined. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExEdgeOrderMode Mode = EPCGExEdgeOrderMode::DirectionSettings;

	/** Defines the direction in which points will be ordered to form the final paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode == EPCGExEdgeOrderMode::DirectionSettings", EditConditionHides))
	FPCGExEdgeDirectionSettings DirectionSettings;

	/** How seed points select their starting node in the cluster. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExEdgeOrderMode::DirectionSettings", EditConditionHides))
	FPCGExNodeSelectionDetails SeedPicking = FPCGExNodeSelectionDetails(200);

	/** Invert the traversal direction (edges point toward seeds instead of away from them). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExEdgeOrderMode::DirectionSettings", EditConditionHides))
	bool bInvert = false;

	/** Whether to use an octree for closest node search. Depending on your dataset, this may be faster or slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay, EditCondition="Mode != EPCGExEdgeOrderMode::DirectionSettings", EditConditionHides))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExEdgeOrderElement;
};

struct FPCGExEdgeOrderContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExEdgeOrderElement;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExEdgeOrderElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(EdgeOrder)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExEdgeOrder
{
	class FBatch;

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExEdgeOrderContext, UPCGExEdgeOrderSettings>
	{
		friend class FBatch;

		FPCGExEdgeDirectionSettings DirectionSettings;
		TSharedPtr<PCGExData::TBuffer<int64>> VtxEndpointBuffer;
		TSharedPtr<PCGExData::TBuffer<int64>> EndpointsBuffer;

		// DFS traversal state
		TArray<int32> Seeded;          // Lowest seed index that picked each node (MAX_int32 = none)
		TArray<int32> SeedClosestNode; // Node each seed picked (-1 = none)
		TArray<int32> Depths;
		TArray<int32> CollectedSeeds;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;
		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;

		void RunDFS();
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

		FPCGExEdgeDirectionSettings DirectionSettings;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
	};
}
