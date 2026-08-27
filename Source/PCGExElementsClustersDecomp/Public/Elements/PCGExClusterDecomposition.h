// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExClusterMT.h"
#include "Core/PCGExClustersProcessor.h"
#include "Core/PCGExDecompositionOperation.h"

#include "PCGExClusterDecomposition.generated.h"

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(Keywords = "decompose partition cell"), meta=(PCGExNodeLibraryDoc="clusters/analyze/cluster-decomposition"))
class UPCGExClusterDecompositionSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ClusterDecomposition, "Cluster : Decomposition", "Decompose clusters into cells and write a CellID attribute on nodes.",
	                                 (Decomposition ? FName(Decomposition.GetClass()->GetMetaData(TEXT("DisplayName"))) : FName("...")));

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(ClusterOp);
	}
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

	virtual bool SupportsDataStealing() const override
	{
		return true;
	}

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual PCGExData::EIOInit GetMainOutputInitMode() const override;
	virtual PCGExData::EIOInit GetEdgeOutputInitMode() const override;
	//~End UPCGExPointsProcessorSettings

	/** The decomposition algorithm to use. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Settings, Instanced, meta = (PCG_Overridable, NoResetToDefault, ShowOnlyInnerProperties))
	TObjectPtr<UPCGExDecompositionInstancedFactory> Decomposition;

	/** Scoring mode for combining multiple heuristics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** Attribute name for the decomposition cell ID written to each node. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FName CellIDAttributeName = FName("CellID");

	/** Optional attribute name for per-node cell count (how many nodes share this cell). Empty = disabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FName CellCountAttributeName = NAME_None;

	/** Optional attribute name for per-node cell bounds size (FVector, full extent). Empty = disabled.
	 *
	 *  The meaning is algorithm-specific:
	 *  - Box-like algorithms (Grid, Max Boxes, BSP Occupancy) report their quantized cell box. For the
	 *    three that have a transform space (Max Boxes, Max Boxes Extended, BSP Occupancy) that box is
	 *    measured along the grid's LOCAL axes, so with Best Fit or Custom it is in that rotated (and,
	 *    for a scaled Custom transform, scaled) frame and is NOT world-axis-aligned. Grid has no
	 *    transform space: its boxes are world-axis-aligned. The value is the enclosing box: a non-cuboid cell (an L-shaped BSP Occupancy region, or a
	 *    Grid cell merged from non-adjacent cells) reports the AABB that contains it, which can exceed
	 *    its filled volume.
	 *  - Node-grouping algorithms (Convex BSP, Spectral, Threshold) report the world-space AABB of the
	 *    member node positions, so a single-node cell reports a zero-size vector.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FName CellSizeAttributeName = NAME_None;

private:
	friend class FPCGExClusterDecompositionElement;
};

struct FPCGExClusterDecompositionContext final : FPCGExClustersProcessorContext
{
	friend class FPCGExClusterDecompositionElement;

	UPCGExDecompositionInstancedFactory* Decomposition = nullptr;

protected:
	PCGEX_ELEMENT_BATCH_EDGE_DECL
};

class FPCGExClusterDecompositionElement final : public FPCGExClustersProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(ClusterDecomposition)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExClusterDecomposition
{
	const FName SourceOverridesDecomposition = TEXT("Overrides : Decomposition");

	class FProcessor final : public PCGExClusterMT::TProcessor<FPCGExClusterDecompositionContext, UPCGExClusterDecompositionSettings>
	{
		friend class FBatch;

		TSharedPtr<PCGExData::TBuffer<int32>> CellIDBuffer;
		TSharedPtr<PCGExData::TBuffer<int32>> CellCountBuffer;
		TSharedPtr<PCGExData::TBuffer<FVector>> CellSizeBuffer;

	public:
		FProcessor(const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
			: TProcessor(InVtxDataFacade, InEdgeDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void CompleteWork() override;
		virtual void Cleanup() override;

	protected:
		TSharedPtr<FPCGExDecompositionOperation> Operation;
	};

	class FBatch final : public PCGExClusterMT::TBatch<FProcessor>
	{
		TSharedPtr<PCGExData::TBuffer<int32>> CellIDBuffer;
		TSharedPtr<PCGExData::TBuffer<int32>> CellCountBuffer;
		TSharedPtr<PCGExData::TBuffer<FVector>> CellSizeBuffer;

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
