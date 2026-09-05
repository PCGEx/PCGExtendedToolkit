// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClusterFilter.h"
#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathIntersectionDetails.h"

#include "PCGExCutClusters.generated.h"

namespace PCGExDetails
{
	class FDistances;
}

UENUM()
enum class EPCGExCutEdgesMode : uint8
{
	Nodes         = 0 UMETA(DisplayName = "Nodes", ToolTip="Check for path overlap with nodes"),
	Edges         = 1 UMETA(DisplayName = "Edges", ToolTip="Check for path overlap with edges"),
	NodesAndEdges = 2 UMETA(DisplayName = "Edges & Nodes", ToolTip="Check for overlap with both nodes and edges"),
};

namespace PCGExCutEdges
{
	const FName SourceNodeFilters = FName("NodeFilters");
	const FName SourceEdgeFilters = FName("EdgeFilters");
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/interop/cluster-cut"))
class UPCGExCutEdgesSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(CutEdges, "Cluster : Cut", "Cut clusters nodes & edges using paths.");
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	/** Intersection detection settings for path-to-edge overlap. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExPathEdgeIntersectionDetails IntersectionDetails = FPCGExPathEdgeIntersectionDetails(false);

	/** Keep intersections/proximity instead of removing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvert = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	EPCGExCutEdgesMode Mode = EPCGExCutEdgesMode::NodesAndEdges;

	/** Widens the node's local bounds before they are transformed, so the expansion scales with the point. Only widens the candidate search, never the distance that decides a cut. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExCutEdgesMode::Edges"))
	double NodeExpansion = 1;

	/** How to measure distance from path to node center vs bounds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExCutEdgesMode::Edges"))
	EPCGExDistance NodeDistanceSettings = EPCGExDistance::Center;

	/** When a node is affected, also mark its connected edges as affected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Mode != EPCGExCutEdgesMode::Edges"))
	bool bAffectedNodesAffectConnectedEdges = false;

	/** When an edge is affected, also mark its endpoint nodes as affected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="!bInvert && Mode != EPCGExCutEdgesMode::Nodes"))
	bool bAffectedEdgesAffectEndpoints = false;

	/** When inverting, keep edges that connect two preserved nodes even if the edge itself wasn't preserved. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bInvert && Mode != EPCGExCutEdgesMode::Edges"))
	bool bKeepEdgesThatConnectValidNodes = false;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

private:
	friend class FPCGExCutEdgesElement;
};

struct FPCGExCutEdgesContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExCutEdgesElement;

	FPCGExPathEdgeIntersectionDetails IntersectionDetails;

	bool bWantsVtxProcessing = false;
	bool bWantsEdgesProcessing = false;

	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> EdgeFilterFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> VtxFilterFactories;

	TArray<TSharedRef<PCGExData::FFacade>> PathFacades;
	TArray<TSharedRef<PCGExPaths::FPath>> Paths;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExCutEdgesElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(CutEdges)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExCutEdges
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExCutEdgesContext, UPCGExCutEdgesSettings>
	{
	protected:
		int8 EdgesProcessed = 0;
		int8 NodesProcessed = 0;

		virtual TSharedPtr<PCGExClusters::FCluster> HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef) override;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
			DefaultEdgeFilterValue = false;
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		virtual void ProcessEdges(const PCGExMT::FScope& Scope) override;

		virtual void ProcessNodes(const PCGExMT::FScope& Scope) override;

		virtual void OnEdgesProcessingComplete() override;
		virtual void OnNodesProcessingComplete() override;
		void TryConsolidate();
		virtual void ProcessRange(const PCGExMT::FScope& Scope) override;
		virtual void CompleteWork() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, const TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
			: TBatch(InContext, InVtx, InEdges)
		{
			PCGEX_TYPED_CONTEXT_AND_SETTINGS(CutEdges)
			bRequiresGraphBuilder = true;
			bAllowVtxDataFacadeScopedGet = true;
			DefaultVtxFilterValue = false;
		}
	};
}
