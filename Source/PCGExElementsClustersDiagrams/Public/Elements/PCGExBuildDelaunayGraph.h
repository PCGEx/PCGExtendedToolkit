// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Graphs/PCGExGraphDetails.h"
#include "PCGExBuildDelaunayGraph.generated.h"

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExMath
{
	namespace Geo
	{
		class TDelaunay3;
	}

	class TDelaunay3;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-delaunay-3d"))
class UPCGExBuildDelaunayGraphSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildDelaunayGraph, "Cluster : Delaunay 3D", "Create a 3D delaunay tetrahedralization for each input dataset.");

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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sites", meta = (PCG_Overridable))
	bool bOutputSites = false;

	/** Mark points that lie on the hull. Edges are never marked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sites", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bMarkSiteHull = false;

	/** Name of the attribute to output the Hull boolean to. True if point is on the hull, otherwise false. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sites", meta = (PCG_Overridable, EditCondition="bMarkSiteHull"))
	FName SiteHullAttributeName = "bIsOnHull";

	/** Merge adjacent sites into a single point */
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Sites", meta = (PCG_Overridable, EditCondition="bUrquhart && bOutputSites", EditConditionHides))
	bool bMergeUrquhartSites = false;

	/** Mark points that lie on the hull. Edges are never marked. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bMarkHull = false;

	/** Name of the attribute to output the Hull boolean to. True if point is on the hull, otherwise false. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="bMarkHull"))
	FName HullAttributeName = "bIsOnHull";

	/** Graph & Edges output properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails(EPCGExMinimalAxis::X);

private:
	friend class FPCGExBuildDelaunayGraphElement;
};

struct FPCGExBuildDelaunayGraphContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBuildDelaunayGraphElement;

	TSharedPtr<PCGExData::FPointIOCollection> MainSites;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBuildDelaunayGraphElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildDelaunayGraph)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildDelaunayGraph
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBuildDelaunayGraphContext, UPCGExBuildDelaunayGraphSettings>
	{
		friend class FOutputDelaunaySites;
		friend class FOutputDelaunayUrquhartSites;

	protected:
		TSharedPtr<TArray<int32>> OutputIndices;
		TSharedPtr<PCGExMath::Geo::TDelaunay3> Delaunay;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;
		TSet<uint64> UrquhartEdges;

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
