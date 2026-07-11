// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClustersProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"

#include "PCGExConnectClusters.generated.h"

class FPCGExPointIOMerger;

namespace PCGExGraphs
{
	class FGraphPatcher;
}

UENUM()
enum class EPCGExBridgeClusterMethod : uint8
{
	Delaunay3D = 0 UMETA(DisplayName = "Delaunay 3D", ToolTip="Uses Delaunay 3D graph to find connections."),
	Delaunay2D = 1 UMETA(DisplayName = "Delaunay 2D", ToolTip="Uses Delaunay 2D graph to find connections."),
	LeastEdges = 2 UMETA(DisplayName = "Least Edges", ToolTip="Ensure all clusters are connected using the least possible number of bridges."),
	MostEdges  = 3 UMETA(DisplayName = "Most Edges", ToolTip="Each cluster will have a bridge to every other cluster"),
	// 4 was the never-implemented "Node Filters" placeholder; "Cluster : Connect Vtx" covers it now.
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/refine/cluster-connect"))
class UPCGExConnectClustersSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ConnectClusters, "Cluster : Connect", "Connects isolated edge clusters by their closest vertices, if they share the same vtx group.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterOp);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings


	/** Method used to find & insert bridges */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(DisplayName="Connect Method"))
	EPCGExBridgeClusterMethod BridgeMethod = EPCGExBridgeClusterMethod::Delaunay3D;

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="BridgeMethod == EPCGExBridgeClusterMethod::Delaunay2D", EditConditionHides))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Meta filter settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;

	/** Write the number of bridges connected to each vertex. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagVtxConnector = false;

	/** Attribute name for the vertex bridge count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bFlagVtxConnector"))
	FName VtxConnectorFlagName = "NumBridges";

	/** Flag edges that are bridges between clusters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagEdgeConnector = false;

	/** Attribute name for the bridge edge flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bFlagEdgeConnector"))
	FName EdgeConnectorFlagName = "IsBridge";

	/** If enabled, won't throw a warning if no bridge could be created. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietNoBridgeWarning = false;

private:
	friend class FPCGExConnectClustersElement;
};

struct FPCGExConnectClustersContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExConnectClustersElement;
	friend class FPCGExCreateBridgeTask;

	FPCGExGeo2DProjectionDetails ProjectionDetails;
	FPCGExCarryOverDetails CarryOverDetails;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExConnectClustersElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ConnectClusters)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExConnectClusters
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExConnectClustersContext, UPCGExConnectClustersSettings>
	{
	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
	public:
		// Connectivity-aware patcher: owns the per-component edge merging + endpoint patching.
		TSharedPtr<PCGExGraphs::FGraphPatcher> Patcher;
		TSet<uint64> Bridges;               // connected cluster-index pairs (from the bridge method)
		TArray<uint64> BridgesList;
		TArray<int32> BridgeEdgeHandles;    // patcher AddEdge handles (parallel to BridgeEndpoints)
		TArray<uint64> BridgeEndpoints;     // H64(VtxA, VtxB) per staged bridge, for the optional connector flags

		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void CompleteWork() override;
		virtual void Write() override;

	protected:
		// Brute-force closest vtx pair between two valid clusters. Returns false if none found.
		bool FindClosestVtxPair(int32 FromClusterIndex, int32 ToClusterIndex, int32& OutVtxA, int32& OutVtxB) const;
	};
}
