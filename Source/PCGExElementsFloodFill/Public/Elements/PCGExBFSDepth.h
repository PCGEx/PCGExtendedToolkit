// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/PCGExScopedContainers.h"

#include "Core/PCGExBFSTriggerCount.h"
#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExFloodFillEdgeDirection.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "PCGExBFSDepth.generated.h"

#define PCGEX_FOREACH_FIELD_BFS_DEPTH(MACRO)\
MACRO(Depth, int32, -1)\
MACRO(Distance, double, -1)\
MACRO(SeedIndex, int32, -1)

namespace PCGExBFSDepth
{
	namespace Labels
	{
		const FName SourceTriggerFiltersLabel = FName("TriggerFilters");
	}
}

UENUM()
enum class EPCGExBFSNormalizedDepthMode : uint8
{
	Global  = 0 UMETA(DisplayName = "Global", ToolTip="depth / MaxDepth. Uniform gradient from seed (0) to deepest node (1)."),
	Cascade = 1 UMETA(DisplayName = "Cascade", ToolTip="Hierarchical falloff. 1.0 at seed, 0.0 at leaf endpoints. Branches inherit from the trunk and smoothly decay toward their own leaves."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-bfs-depth"))
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
	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

	virtual bool SupportsEdgeSorting() const override
	{
		return EdgeDirectionOutput.RequiresSortingRules();
	}

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

	/** Write the point index of the closest seed that reached this vertex. Unreachable vertices keep a value of -1. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteSeedIndex = false;

	/** Name of the 'int32' attribute to write seed index to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Seed Index", PCG_Overridable, EditCondition="bWriteSeedIndex"))
	FName SeedIndexAttributeName = FName("SeedIndex");

	/** Write a normalized depth value (0-1) to vertices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNormalizedDepth = false;

	/** Name of the 'double' attribute to write normalized depth to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Normalized Depth", PCG_Overridable, EditCondition="bWriteNormalizedDepth"))
	FName NormalizedDepthAttributeName = FName("NormalizedBFSDepth");

	/** How to normalize the depth values. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_NotOverridable, EditCondition="bWriteNormalizedDepth", EditCondtionHides, HideEditConditionToggle))
	EPCGExBFSNormalizedDepthMode NormalizedDepthMode = EPCGExBFSNormalizedDepthMode::Global;

	/** Output the BFS traversal direction onto each edge, in propagation order (away from the seed). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable))
	FPCGExFloodFillEdgeDirectionDetails EdgeDirectionOutput;

	/** Output a running count of "trigger" vtx (those passing the Vtx Filters) crossed along the BFS path. Adds a required Vtx Filters input. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable))
	FPCGExBFSTriggerCountDetails TriggerCount;

	/** Which Seed attributes to forward on the vtx they reached (each vtx receives values from its owning seed). Disabled by default. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable))
	FPCGExForwardDetails SeedForwarding = FPCGExForwardDetails(false);

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

	// Adjusted copy of SeedForwarding; each batch builds its own prepared handler from it (writers
	// target the batch's vtx facade and must be created single-threaded at batch scope).
	FPCGExForwardDetails SeedForwardDetails;

	PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_DECL_TOGGLE)

	FPCGExFloodFillEdgeDirectionDetails EdgeDirectionOutput;

	FPCGExBFSTriggerCountDetails TriggerCount;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;

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
		TArray<int32> SeedOwners;
		TArray<int32> Parents;    // BFS tree parent per node (-1 = seed/root)
		TArray<int32> ChildCount; // Number of BFS children per node
		int32 MaxBFSDepth = 0;

		TSharedPtr<PCGExMT::TScopedArray<FIntPoint>> SeedNodeIndices;
		TArray<FIntPoint> CollectedSeeds;

		FPCGExFloodFillEdgeDirectionDetails EdgeDirectionDetails;

		const FPCGExBFSTriggerCountDetails* TriggerCountPtr = nullptr; // Points at the batch's instance (shared vtx writer)
		TArray<int32> TriggerCounts;                                   // BFS-path trigger count per node

		// Batch-owned prepared forward handler (writers pre-created on the shared vtx facade); null when forwarding is off.
		TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;

		void ComputeNormalizedDepth();

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		int32* DepthData = nullptr;
		double* DistanceData = nullptr;
		int32* SeedIndexData = nullptr;
		double* NormalizedDepthData = nullptr;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;

		void RunBFS();
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		PCGEX_FOREACH_FIELD_BFS_DEPTH(PCGEX_OUTPUT_DECL)
		TSharedPtr<PCGExData::TBuffer<double>> NormalizedDepthWriter;
		// Built once (single-threaded) so the per-processor BFS passes never create writables on the shared vtx facade.
		TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;

	public:
		FPCGExFloodFillEdgeDirectionDetails EdgeDirectionOutput;
		FPCGExBFSTriggerCountDetails TriggerCount;

		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
