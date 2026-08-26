// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Math/PCGExProjectionDetails.h"

#include "PCGExBuildDelaunayGraph2D.generated.h"

namespace PCGExMath
{
	namespace Geo
	{
		class TDelaunay2;
	}

	class TDelaunay2;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UENUM()
enum class EPCGExUrquhartSiteMergeMode : uint8
{
	None       = 0 UMETA(DisplayName = "None", ToolTip="Do not merge sites."),
	MergeSites = 1 UMETA(DisplayName = "Merge Sites", ToolTip="Merge site is the average of the merge."),
	MergeEdges = 2 UMETA(DisplayName = "Merge Edges", ToolTip="Merge site is the averge of the removed edges."),
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(Keywords = "triangulation", PCGExNodeLibraryDoc="clusters/generate/cluster-delaunay-2d"))
class UPCGExBuildDelaunayGraph2DSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildDelaunayGraph2D, "Cluster : Delaunay 2D", "Create a 2D delaunay triangulation for each input dataset.");

	virtual TArray<FText> GetNodeTitleAliases() const override;
	
	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}
#endif
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	//~End UPCGExPointsProcessorSettings

	/** Output the Urquhart graph of the Delaunay triangulation (removes the longest edge of each Delaunay cell) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bUrquhart = false;

	/** Output delaunay sites */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable))
	bool bOutputSites = false;

	/** Mark points that lie on the hull. Edges are never marked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bMarkSiteHull = false;

	/** Name of the attribute to output the Hull boolean to. True if point is on the hull, otherwise false. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, EditCondition="bMarkSiteHull"))
	FName SiteHullAttributeName = "bIsOnHull";

	/** Merge adjacent sites into a single point */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, EditCondition="bUrquhart && bOutputSites", EditConditionHides))
	EPCGExUrquhartSiteMergeMode UrquhartSitesMerge = EPCGExUrquhartSiteMergeMode::None;

	/** Mark points that lie on the hull. Edges are never marked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bMarkHull = false;

	/** Name of the attribute to output the Hull boolean to. True if point is on the hull, otherwise false. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bMarkHull"))
	FName HullAttributeName = "bIsOnHull";

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails(EPCGExMinimalAxis::X);

private:
	friend class FPCGExBuildDelaunayGraph2DElement;
};

struct FPCGExBuildDelaunayGraph2DContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBuildDelaunayGraph2DElement;

	TSharedPtr<PCGExData::FPointIOCollection> MainSites;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBuildDelaunayGraph2DElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildDelaunayGraph2D)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildDelaunayGraph2D
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBuildDelaunayGraph2DContext, UPCGExBuildDelaunayGraph2DSettings>
	{
		friend class FOutputDelaunaySites2D;
		friend class FOutputDelaunayUrquhartSites2D;

	protected:
		TSharedPtr<TArray<int32>> OutputIndices;
		TSharedPtr<PCGExMath::Geo::TDelaunay2> Delaunay;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;
		TSet<uint64> UrquhartEdges;
		FPCGExGeo2DProjectionDetails ProjectionDetails;

		TSharedPtr<PCGExData::TBuffer<bool>> HullMarkPointWriter;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;

		virtual void Write() override;
		virtual void Output() override;
	};
}
