// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Containers/PCGExScopedContainers.h"

#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExFloodFill.h"
#include "Core/PCGExDiffusionGrowthProcessor.h"
#include "Core/PCGExFloodFillEdgeDirection.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Sampling/PCGExSamplingCommon.h"
#include "PCGExFloodFillClusters.generated.h"

#define PCGEX_FOREACH_FIELD_CLUSTER_DIFF(MACRO)\
MACRO(DiffusionDepth, int32, -1)\
MACRO(NormalizedDiffusionDepth, double, 0)\
MACRO(DiffusionOrder, int32, -1)\
MACRO(DiffusionDistance, double, 0)\
MACRO(DiffusionEnding, bool, false)

class UPCGExBlendOpFactory;

UENUM()
enum class EPCGExFloodFillPathOutput : uint8
{
	None       = 0 UMETA(DisplayName = "None", ToolTip="Don't output any paths."),
	Full       = 1 UMETA(DisplayName = "Full", ToolTip="Output full paths, from seed to end point -- generate a lot of overlap."),
	Partitions = 2 UMETA(DisplayName = "Partitions", ToolTip="Output partial paths, only endpoints will overlap. "),
};

UENUM()
enum class EPCGExFloodFillPathPartitions : uint8
{
	Length = 0 UMETA(DisplayName = "Length", ToolTip="TBD"),
	Score  = 1 UMETA(DisplayName = "Score", ToolTip="TBD"),
	Depth  = 2 UMETA(DisplayName = "Depth", ToolTip="TBD"),
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill"))
class UPCGExClusterDiffusionSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

	//~Begin UObject interface
public:
	UPCGExClusterDiffusionSettings(const FObjectInitializer& ObjectInitializer);

	//~End UObject interface

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ClusterFloodFill, "Cluster : Flood Fill", "Diffuses vtx attributes onto their neighbors.");
#endif

	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;

#if WITH_EDITOR
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
#endif

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
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Seeds settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	FPCGExFloodFillSeedPickingDetails Seeds;

	/** Defines how each vtx is diffused */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFloodFillProcessing Processing = EPCGExFloodFillProcessing::Parallel;

	/** Diffusion settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	FPCGExFloodFillFlowDetails Diffusion;

#pragma region  Outputs

	/** Write the diffusion depth the vtx was subjected to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDiffusionDepth = false;

	/** Name of the 'int32' attribute to write diffusion depth to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Diffusion Depth", PCG_Overridable, EditCondition="bWriteDiffusionDepth"))
	FName DiffusionDepthAttributeName = FName("DiffusionDepth");

	/** Write the normalized diffusion depth (0-1), relative to the diffusion's max depth. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteNormalizedDiffusionDepth = false;

	/** Name of the 'double' attribute to write normalized diffusion depth to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Normalized Diffusion Depth", PCG_Overridable, EditCondition="bWriteNormalizedDiffusionDepth"))
	FName NormalizedDiffusionDepthAttributeName = FName("NormalizedDiffusionDepth");

	/** Write the final diffusion order. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDiffusionOrder = false;

	/** Name of the 'int32' attribute to write order to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Diffusion Order", PCG_Overridable, EditCondition="bWriteDiffusionOrder"))
	FName DiffusionOrderAttributeName = FName("DiffusionOrder");

	/** Write the final diffusion distance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDiffusionDistance = false;

	/** Name of the 'double' attribute to write diffusion distance to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Diffusion Distance", PCG_Overridable, EditCondition="bWriteDiffusionDistance"))
	FName DiffusionDistanceAttributeName = FName("DiffusionDistance");

	/** Write on the vtx whether it's a diffusion "endpoint". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bWriteDiffusionEnding = false;

	/** Name of the 'bool' attribute to write diffusion ending to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(DisplayName="Diffusion Ending", PCG_Overridable, EditCondition="bWriteDiffusionEnding"))
	FName DiffusionEndingAttributeName = FName("DiffusionEnding");

	/** Output the diffusion traversal direction onto each edge, in propagation order (away from the seed). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta=(PCG_Overridable))
	FPCGExFloodFillEdgeDirectionDetails EdgeDirectionOutput;

#pragma endregion

	/** Which Seed attributes to forward on the vtx they diffused to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs", meta = (PCG_Overridable))
	FPCGExForwardDetails SeedForwarding = FPCGExForwardDetails(true);


	/** Controls how flood fill results are output as paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths")
	EPCGExFloodFillPathOutput PathOutput = EPCGExFloodFillPathOutput::None;

	/** Criteria used to partition paths into separate outputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths", meta=(DisplayName=" ├─ Partition over", EditCondition="PathOutput == EPCGExFloodFillPathOutput::Partitions"))
	EPCGExFloodFillPathPartitions PathPartitions = EPCGExFloodFillPathPartitions::Depth;

	/** Sort direction for partitioned output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths", meta=(DisplayName=" └─ Sorting", EditCondition="PathOutput == EPCGExFloodFillPathOutput::Partitions"))
	EPCGExSortDirection PartitionSorting = EPCGExSortDirection::Descending;

	/** Write the normalized path depth (0-1) on output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths", meta=(PCG_Overridable, EditCondition="PathOutput != EPCGExFloodFillPathOutput::None"))
	bool bWriteNormalizedPathDepth = false;

	/** Name of the 'double' attribute to write normalized path depth to.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths", meta=(DisplayName="Normalized Path Depth", PCG_Overridable, EditCondition="bWriteNormalizedPathDepth && PathOutput != EPCGExFloodFillPathOutput::None"))
	FName NormalizedPathDepthAttributeName = FName("NormalizedPathDepth");

	/** How to normalize the path depth. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths", meta=(PCG_NotOverridable, EditCondition="bWriteNormalizedPathDepth && PathOutput != EPCGExFloodFillPathOutput::None"))
	EPCGExFloodFillNormalizedPathDepthMode NormalizedPathDepthMode = EPCGExFloodFillNormalizedPathDepthMode::FullPath;

	/** Copy seed point attributes as tags on output paths. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Outputs - Paths", meta=(EditCondition="PathOutput != EPCGExFloodFillPathOutput::None"))
	FPCGExAttributeToTagDetails SeedAttributesToPathTags;


	/** Whether or not to search for closest node using an octree. Depending on your dataset, enabling this may be either much faster, or much slower. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Performance, meta=(PCG_NotOverridable, AdvancedDisplay))
	bool bUseOctreeSearch = false;

private:
	friend class FPCGExClusterDiffusionElement;
};

struct FPCGExClusterDiffusionContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExClusterDiffusionElement;

	TArray<TObjectPtr<const UPCGExBlendOpFactory>> BlendingFactories;
	TArray<TObjectPtr<const UPCGExFillControlsFactoryData>> FillControlFactories;

	TSharedPtr<PCGExData::FFacade> SeedsDataFacade;
	FPCGExAttributeToTagDetails SeedAttributesToPathTags;
	TSharedPtr<PCGExData::FDataForwardHandler> SeedForwardHandler;

	TSharedPtr<PCGExData::FPointIOCollection> Paths;

	PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_DECL_TOGGLE)

	FPCGExFloodFillEdgeDirectionDetails EdgeDirectionOutput;

	int32 ExpectedPathCount = 0;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExClusterDiffusionElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ClusterDiffusion)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExClusterDiffusion
{
	class FBatch;
	class FProcessor;

	class FProcessor final : public PCGExFloodFill::TDiffusionGrowthProcessor<FPCGExClusterDiffusionContext, UPCGExClusterDiffusionSettings>
	{
		friend FBatch;

	protected:
		const PCGExClusters::FNode* RoamingSeedNode = nullptr;
		const PCGExClusters::FNode* RoamingGoalNode = nullptr;

		// Shared per-vtx claim array, provided to the growth base via GetInfluencesCount().
		TSharedPtr<TArray<int8>> InfluencesCount;
		TSharedPtr<PCGExBlending::FBlendOpsManager> BlendOpsManager;

		TSharedPtr<PCGExFloodFill::FDiffusionPathWriter> PathWriter;
		TSharedPtr<TArray<int32>> DiffusionDepths; // Vtx point index -> diffusion depth, for NormalizedPathDepth

		TSharedPtr<PCGExMT::TScopedNumericValue<double>> MaxDistanceValue;

		FPCGExFloodFillEdgeDirectionDetails EdgeDirectionDetails;

		int32 ExpectedPathCount = 0;

		//~ TDiffusionGrowthProcessor seams
		virtual bool OnGrowthSetup() override;
		virtual TSharedPtr<TArray<int8>> GetInfluencesCount() const override;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TDiffusionGrowthProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
			bAllowEdgesDataFacadeScopedGet = false;
		}

		PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_DECL)

		virtual ~FProcessor() override;

		virtual void CompleteWork() override;
		void Diffuse(const TSharedPtr<PCGExFloodFill::FDiffusion>& Diffusion);

		void OnDiffusionComplete();

		virtual void Write() override;
		virtual void Cleanup() override;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		PCGEX_FOREACH_FIELD_CLUSTER_DIFF(PCGEX_OUTPUT_DECL)

	protected:
		TSharedPtr<TArray<int8>> InfluencesCount;
		TSharedPtr<PCGExBlending::FBlendOpsManager> BlendOpsManager;
		TSharedPtr<TArray<int32>> DiffusionDepths; // Vtx point index -> diffusion depth, for NormalizedPathDepth
		TSharedPtr<PCGExDetails::TSettingValue<int32>> FillRate;

	public:
		FPCGExFloodFillEdgeDirectionDetails EdgeDirectionOutput;

		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);
		virtual ~FBatch() override;

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual void Process() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
