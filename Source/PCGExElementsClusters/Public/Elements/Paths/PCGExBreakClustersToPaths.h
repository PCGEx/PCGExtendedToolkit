// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Factories/PCGExFactories.h"

#include "Core/PCGExClustersProcessor.h"
#include "Math/PCGExWinding.h"

#include "PCGExBreakClustersToPaths.generated.h"

namespace PCGExClusters
{
	class FNodeChain;
}

UENUM()
enum class EPCGExBreakClusterOperationTarget : uint8
{
	Paths = 0 UMETA(DisplayName = "Paths", ToolTip="Operate on edge chains which form paths with no crossings.  e.g, nodes with only two neighbors."),
	Edges = 1 UMETA(DisplayName = "Edges", ToolTip="Operate on each edge individually (very expensive)"),
};

UENUM()
enum class EPCGExBreakClusterLeavesHandling : uint8
{
	Include = 0 UMETA(DisplayName = "Include Leaves", ToolTip="Include leaves."),
	Exclude = 1 UMETA(DisplayName = "Exclude Leaves", ToolTip="Exclude leaves."),
	Only    = 2 UMETA(DisplayName = "Only Leaves", ToolTip="Only process leaves."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/interop/cluster-break-to-paths"))
class UPCGExBreakClustersToPathsSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BreakClustersToPaths, "Cluster : Break to Paths", "Create individual paths from continuous edge chains.");
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_NAME(ClusterOp); }
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual bool SupportsEdgeSorting() const override { return DirectionSettings.RequiresSortingRules(); }
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	PCGEX_NODE_POINT_FILTER(FName("Break Conditions"), "Filters used to know which points are 'break' points.", PCGExFactories::ClusterNodeFilters, false)
	//~End UPCGExPointsProcessorSettings

	/** How to handle leaves */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBreakClusterLeavesHandling LeavesHandling = EPCGExBreakClusterLeavesHandling::Include;

	/** Operation target mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExBreakClusterOperationTarget OperateOn;

	/** Defines the direction in which points will be ordered to form the final paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGExEdgeDirectionSettings DirectionSettings;

	/** Enforce a winding order for paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExWindingMutation Winding = EPCGExWindingMutation::CounterClockwise;

	/** Whether to apply winding on closed loops only or all paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bWindOnlyClosedLoops = true;

	/** Projection settings. Winding is computed on a 2D plane. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Winding != EPCGExWindingMutation::Unchanged", EditConditionHides))
	FPCGExGeo2DProjectionDetails ProjectionDetails = FPCGExGeo2DProjectionDetails();

	/** Do not output paths that have less points that this value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin=2))
	int32 MinPointCount = 2;

	/** Enable maximum point count filtering for output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bOmitAbovePointCount = false;

	/** Do not output paths that have more points that this value */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bOmitAbovePointCount", ClampMin=2))
	int32 MaxPointCount = 500;

private:
	friend class FPCGExBreakClustersToPathsElement;
};

struct FPCGExBreakClustersToPathsContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExBreakClustersToPathsElement;

	TSharedPtr<PCGExData::FPointIOCollection> OutputPaths;
	TArray<TSharedPtr<PCGExClusters::FNodeChain>> Chains;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExBreakClustersToPathsElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BreakClustersToPaths)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBreakClustersToPaths
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExBreakClustersToPathsContext, UPCGExBreakClustersToPathsSettings>
	{
		friend class FBatch;

	protected:
		TArray<TSharedPtr<PCGExClusters::FNodeChain>> ProcessedChains;
		TArray<TSharedPtr<PCGExData::FPointIO>> ChainsIO;

		FPCGExEdgeDirectionSettings DirectionSettings;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		bool BuildChains();
		virtual void CompleteWork() override;
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;

		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	protected:
		FPCGExEdgeDirectionSettings DirectionSettings;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
			DefaultVtxFilterValue = false;
		}

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
	};
}
