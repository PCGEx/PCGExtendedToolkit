// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExClipper2Processor.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExClipper2Decompose.generated.h"

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExData
{
	class FPointIO;
}

/** Clipper2 : Decompose -- like Clipper2 : Volume but outputs a PCGEx cluster (deduped footprint vtx + deduped union of convex-piece edges) instead of volume actors. */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="clusters/generate/clipper2-decompose"))
class UPCGExClipper2DecomposeSettings : public UPCGExClipper2ProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExClipper2DecomposeSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Clipper2Decompose, "Clipper2 : Decompose", "Decompose a closed path footprint into a convex-cell cluster (boundary + Hertel-Mehlhorn diagonals).");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	//~End UPCGExPointsProcessorSettings

	/** Projection settings used to flatten the path into a 2D footprint before decomposition. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Fill rule used when triangulating the footprint. Even-Odd treats nested rings as holes (donut footprints). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExClipper2FillRule FillRule = EPCGExClipper2FillRule::EvenOdd;

	/** Greedily merge triangles into larger convex pieces (Hertel-Mehlhorn); disable to keep the raw triangulation (every triangle edge). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Decomposition", meta = (PCG_NotOverridable))
	bool bMergeConvexPieces = true;

	/** Safety cap on convex pieces per footprint. Footprints exceeding this are skipped with a warning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Decomposition", meta = (PCG_Overridable, ClampMin = 1))
	int32 MaxConvexPieces = 256;

	/** Graph & Edges output properties. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails(EPCGExMinimalAxis::X);

	/** Suppress per-group warnings about degenerate footprints / failed triangulation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_NotOverridable))
	bool bQuietWarnings = false;

	virtual FPCGExGeo2DProjectionDetails GetProjectionDetails() const override;

	virtual bool SupportOpenMainPaths() const override
	{
		return false; // Decomposition requires closed footprints.
	}

	virtual bool SupportsAutoGrouping() const override
	{
		return true;
	} // outer + nested holes -> one cluster
};

// Per-group build record: authored vtx IO + graph builder, compiled later in OutputWork.
struct FPCGExDecomposeCluster
{
	TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;
	TSharedPtr<PCGExData::FPointIO> VtxIO;
	int32 GroupIndex = 0;
};

struct FPCGExClipper2DecomposeContext final : FPCGExClipper2ProcessorContext
{
	friend class FPCGExClipper2DecomposeElement;

	// Per-group cluster build records produced off-thread in Process(Group).
	TArray<FPCGExDecomposeCluster> StagedClusters;
	mutable FCriticalSection StagedClustersLock;

	void AddStagedCluster(const TSharedPtr<PCGExGraphs::FGraphBuilder>& InGraphBuilder, const TSharedPtr<PCGExData::FPointIO>& InVtxIO, int32 InGroupIndex);

	/** Stage every compiled cluster's vtx + edges, in deterministic group order. */
	void StageClusterOutputs();

	virtual void Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group) override;
};

class FPCGExClipper2DecomposeElement final : public FPCGExClipper2ProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(Clipper2Decompose)

	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
	virtual void OutputWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
