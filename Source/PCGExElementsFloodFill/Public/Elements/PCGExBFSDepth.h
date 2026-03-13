// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/PCGExScopedContainers.h"

#include "Core/PCGExClustersProcessor.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "PCGExBFSDepth.generated.h"

#define PCGEX_FOREACH_FIELD_BFS_DEPTH(MACRO)\
MACRO(Depth, int32, -1)\
MACRO(Distance, double, -1)

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-bfs-depth"))
class UPCGExBFSDepthSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BFSDepth, "Cluster : BFS Depth", "Computes breadth-first search depth and distance from seed points to all reachable vertices.");
#endif

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

protected:
	virtual bool SupportsDataStealing() const override { return true; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** How seed points select their starting node in the cluster. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExNodeSelectionDetails SeedPicking = FPCGExNodeSelectionDetails(200);

	/** Write the BFS depth (hop count from nearest seed). Unreachable vertices keep a value of -1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDepth = true;

	/** Name of the 'int32' attribute to write depth to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Depth", PCG_Overridable, EditCondition="bWriteDepth"))
	FName DepthAttributeName = FName("BFSDepth");

	/** Write the accumulated edge distance from the nearest seed. Unreachable vertices keep a value of -1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDistance = false;

	/** Name of the 'double' attribute to write distance to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Distance", PCG_Overridable, EditCondition="bWriteDistance"))
	FName DistanceAttributeName = FName("BFSDistance");

	/** Whether to use an octree for closest node search. Depending on your dataset, this may be faster or slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExBFSDepthElement;
};

struct FPCGExBFSDepthContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExBFSDepthElement;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;

	PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_DECL_TOGGLE)

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExBFSDepthElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BFSDepth)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBFSDepth
{
	class FBatch;

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExBFSDepthContext, UPCGExBFSDepthSettings>
	{
		friend class FBatch;

	protected:
		TArray<int8> Seeded;
		TArray<int32> Depths;
		TArray<double> Distances;

		TSharedPtr<PCGExMT::TScopedArray<int32>> SeedNodeIndices;
		TArray<int32> CollectedSeeds;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		int32* DepthData = nullptr;
		double* DistanceData = nullptr;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		void RunBFS();
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_DECL)

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
