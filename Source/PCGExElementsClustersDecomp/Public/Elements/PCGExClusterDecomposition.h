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

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(Keywords = "decompose partition cell"), meta=(PCGExNodeLibraryDoc="clusters/cluster-decomposition"))
class UPCGExClusterDecompositionSettings : public UPCGExClustersProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ClusterDecomposition, "Cluster : Decomposition", "Decompose clusters into cells and write a CellID attribute on nodes.",
		(Decomposition ? FName(Decomposition.GetClass()->GetMetaData(TEXT("DisplayName"))) : FName("...")));
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(ClusterOp); }
#endif

	virtual bool IsPinUsedByNodeExecution(const UPCGPin* InPin) const override;

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

	public:
		FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges);

		virtual void RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader) override;
		virtual void OnProcessingPreparationComplete() override;
		virtual bool PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor) override;
		virtual void Write() override;
	};
}
