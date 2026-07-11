// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExClustersProcessor.h"
#include "Data/Utils/PCGExDataFilterDetails.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExConnectVtx.generated.h"

namespace PCGExGraphs
{
	class FGraphPatcher;
	class FVtxMerger;
}

namespace PCGExProbing
{
	class FProbingEngine;
}

namespace PCGExClusterFilter
{
	class FManager;
}

namespace PCGExClusters
{
	class FCluster;
}

class UPCGExProbeFactoryData;
class FPCGExProbeOperation;

UENUM()
enum class EPCGExConnectVtxScope : uint8
{
	VtxGroup = 0 UMETA(DisplayName = "Vtx Group", ToolTip="Probing spans all clusters sharing the same vtx data; new edges may connect different clusters within a group."),
	Cluster  = 1 UMETA(DisplayName = "Cluster", ToolTip="Probing is confined within each cluster; existing cluster boundaries are preserved."),
	All      = 2 UMETA(DisplayName = "All Inputs", ToolTip="All input clusters are merged into a single vtx dataset; new edges may connect anything to anything."),
};

UENUM()
enum class EPCGExConnectVtxEdgeRelation : uint8
{
	Any          = 0 UMETA(DisplayName = "Any", ToolTip="No constraint on which vtx may connect."),
	CrossCluster = 1 UMETA(DisplayName = "Cross-Cluster Only", ToolTip="Only vtx from different clusters may connect."),
	CrossData    = 2 UMETA(DisplayName = "Cross-Data Only", ToolTip="Only vtx from different input vtx datasets may connect. All Inputs scope only."),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/refine/cluster-connect-vtx"))
class UPCGExConnectVtxSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ConnectVtx, "Cluster : Connect Vtx", "Connect cluster vtx according to a set of probes, within or across clusters.");

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
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	/** Which vtx can see each other when probing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExConnectVtxScope Scope = EPCGExConnectVtxScope::VtxGroup;

	/** Additional constraint on which vtx pairs probes may connect. Cluster scope enforces same-cluster connections and ignores this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, EditCondition="Scope != EPCGExConnectVtxScope::Cluster", EditConditionHides))
	EPCGExConnectVtxEdgeRelation EdgeRelation = EPCGExConnectVtxEdgeRelation::Any;

	/** Prevent connections between coincident points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bPreventCoincidence = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bPreventCoincidence", ClampMin=0.00001, ClampMax=1))
	double CoincidenceTolerance = 0.001;

	/** Project points to 2D for connection testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bProjectPoints = false;

	/** Projection settings for 2D testing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bProjectPoints", DisplayName="Project Points"))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Meta filter settings for merged edges. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings"))
	FPCGExCarryOverDetails CarryOverDetails;

	/** Meta filter settings for the merged vtx (All Inputs scope only). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Carry Over Settings - Vtx", EditCondition="Scope == EPCGExConnectVtxScope::All", EditConditionHides))
	FPCGExCarryOverDetails VtxCarryOverDetails;

	/** Write the number of new connections added to each vertex. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagVtxConnector = false;

	/** Attribute name for the vertex connection count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bFlagVtxConnector"))
	FName VtxConnectorFlagName = "NumConnections";

	/** Flag edges that were created by this node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFlagEdgeConnector = false;

	/** Attribute name for the new edge flag. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bFlagEdgeConnector"))
	FName EdgeConnectorFlagName = "IsConnector";

	/** If enabled, won't throw a warning if no connection could be created. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietNoConnectionWarning = false;

private:
	friend class FPCGExConnectVtxElement;
};

namespace PCGExConnectVtx
{
	class FProcessor;
	class FBatch;

	PCGEX_CTX_STATE(State_LaunchingMerge)
	PCGEX_CTX_STATE(State_MergingVtx)
	PCGEX_CTX_STATE(State_Probing)
	PCGEX_CTX_STATE(State_Patching)

	// Shared staging (used by both the per-batch and All-Inputs pipelines). Offset shifts a cluster's
	// vtx point indices into the merged/context domain (0 for a single-source batch); source index tags
	// the owning vtx source for merged-sources endpoint renumbering (INDEX_NONE for single-source).
	void RegisterClusterGroups(PCGExGraphs::FGraphPatcher& InPatcher, const TArray<TSharedPtr<PCGExClusters::FCluster>>& InClusters, const int32 InOffset, const int32 InSourceIndex);

	// Stage probe-produced edges into the patcher, skipping edges that already exist and self-edges.
	void StageProbeEdges(PCGExGraphs::FGraphPatcher& InPatcher, const TSet<uint64>& InNewEdges, const TSet<uint64>& InExistingEdges, TArray<int32>& OutHandles, TArray<uint64>& OutEndpoints);
}

struct FPCGExConnectVtxContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExConnectVtxElement;

	virtual ~FPCGExConnectVtxContext() override;

	TArray<TObjectPtr<const UPCGExProbeFactoryData>> ProbeFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> GeneratorsFiltersFactories;
	TArray<TObjectPtr<const UPCGExPointFilterFactoryData>> ConnectablesFiltersFactories;

	FVector CWCoincidenceTolerance = FVector::OneVector;

	FPCGExCarryOverDetails CarryOverDetails;
	FPCGExCarryOverDetails VtxCarryOverDetails;

	// All Inputs scope: context-level merge -> probe -> patch pipeline state.
	TArray<TSharedPtr<PCGExConnectVtx::FBatch>> ValidBatches;
	TSharedPtr<PCGExGraphs::FVtxMerger> VtxMerger;
	TSharedPtr<PCGExData::FFacade> MergedFacade;
	TSharedPtr<PCGExProbing::FProbingEngine> Engine;
	TSharedPtr<PCGExGraphs::FGraphPatcher> Patcher;
	TArray<int32> GroupIds;
	TSet<uint64> ExistingEdges;
	TArray<int32> ConnectorEdgeHandles;
	TArray<uint64> ConnectorEndpoints;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExConnectVtxElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ConnectVtx)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;

	// All Inputs scope steps (each maps to one context state)
	bool LaunchVtxMerge(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const;
	bool LaunchProbing(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const;
	bool LaunchPatching(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const;
	bool CommitAndOutput(FPCGExConnectVtxContext* Context, const UPCGExConnectVtxSettings* Settings) const;
};

namespace PCGExConnectVtx
{
	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExConnectVtxContext, UPCGExConnectVtxSettings>
	{
		TSharedPtr<PCGExClusterFilter::FManager> GeneratorsFilter;
		TSharedPtr<PCGExClusterFilter::FManager> ConnectablesFilter;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;

		// Writes this cluster's participation masks (point-index space) into the batch caches.
		// Called sequentially from FBatch::CompleteWork so shared-vtx writes are deterministic.
		void ContributeMasks(const TSharedPtr<TArray<int8>>& InCanGenerate, const TSharedPtr<TArray<int8>>& InAcceptConnections) const;

		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		friend class FProcessor;

	public:
		// Participation masks in vtx point-index space, filled per-cluster by the processors.
		// Non-members (orphan vtx) stay 0 and never take part in probing.
		TSharedPtr<TArray<int8>> CanGenerateCache;
		TSharedPtr<TArray<int8>> AcceptConnectionsCache;

		// Per-point cluster index (ValidClusters order; INDEX_NONE for orphans) & existing edge hashes.
		TArray<int32> ClusterIds;
		TSet<uint64> ExistingEdges;

		// All Inputs scope: this batch's ranges in the merged/context domain.
		int32 MergeOffset = 0;
		int32 GroupIdOffset = 0;

		// Per-batch scopes (Vtx Group / Cluster)
		TSharedPtr<PCGExProbing::FProbingEngine> Engine;
		TSharedPtr<PCGExGraphs::FGraphPatcher> Patcher;
		TArray<int32> ConnectorEdgeHandles;
		TArray<uint64> ConnectorEndpoints;

		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void Process() override;
		virtual void CompleteWork() override;
		virtual void Write() override;

	protected:
		// Fills ClusterIds, ExistingEdges & the participation masks from ValidClusters (all scopes).
		void BuildTopologyAndMasks();

		// Forward vtx + all paired edges untouched (batch has no usable clusters to connect).
		void ForwardUntouched();

		void StageAndResolve();
	};
}
