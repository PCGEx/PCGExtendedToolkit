// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Math/PCGExProjectionDetails.h"
#include "Math/Geo/PCGExGeo.h"
#include "Math/Geo/PCGExVoronoi.h"
#include "Utils/PCGValueRange.h"

#include "PCGExBuildVoronoiGraph2D.generated.h"

namespace PCGExGraphs
{
	class FGraphBuilder;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

/** Parameters for conducting a sweep with a specified shape against the physical world. */
USTRUCT(BlueprintType)
struct FPCGExVoronoiSitesOutputDetails
{
	GENERATED_BODY()

	FPCGExVoronoiSitesOutputDetails() = default;

	/** Write the number of Voronoi cell vertices influenced by each site. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteInfluencesCount = false;

	/** Attribute name for the influences count. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bWriteInfluencesCount"))
	FName InfluencesCountAttributeName = "InfluencesCount";

	/** Write the minimum distance from each site to its cell vertices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteMinRadius = false;

	/** Attribute name for the minimum radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bWriteMinRadius"))
	FName MinRadiusAttributeName = "MinRadius";

	/** Write the maximum distance from each site to its cell vertices. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteMaxRadius = false;

	/** Attribute name for the maximum radius. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bWriteMaxRadius"))
	FName MaxRadiusAttributeName = "MaxRadius";

	bool Validate(FPCGExContext* InContext) const;

	TArray<FVector> Locations;
	TArray<int32> Influences;

	void Init(const TSharedPtr<PCGExData::FFacade>& InSiteFacade);
	void AddInfluence(const int32 SiteIndex, const FVector& SitePosition, const int32 Count = 1);
	void Output(const int32 SiteIndex);

protected:
	bool bWantsDist = false;
	TConstPCGValueRange<FTransform> InTransforms;

	TSharedPtr<TArray<double>> MinRadius;
	TSharedPtr<TArray<double>> MaxRadius;

	TSharedPtr<PCGExData::TBuffer<double>> MinRadiusWriter;
	TSharedPtr<PCGExData::TBuffer<double>> MaxRadiusWriter;
	TSharedPtr<PCGExData::TBuffer<int32>> InfluenceCountWriter;
};

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-voronoi-2d"))
class UPCGExBuildVoronoiGraph2DSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(BuildVoronoiGraph2D, "Cluster : Voronoi 2D", "Create a 2D Voronoi graph for each input dataset.");

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

	/** Distance metric used for the Voronoi diagram. Euclidean produces classic Voronoi, Manhattan/Chebyshev produce axis-aligned edge structures. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExVoronoiMetric Metric = EPCGExVoronoiMetric::Euclidean;

	/** Method used to find Voronoi cell location */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExCellCenter Method = EPCGExCellCenter::Centroid;

	/** Bounds used for point pruning & balanced centroid. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double ExpandBounds = 100;

	/** Prune points outside bounds */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Method == EPCGExCellCenter::Circumcenter"))
	bool bPruneOutOfBounds = false;

	/** Projection settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Graph & Edges output properties. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails = FPCGExGraphBuilderDetails(EPCGExMinimalAxis::X);

	/** Whether to output updated sites */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable))
	bool bOutputSites = true;


	/** If enabled, sites whose cell is open -- on the hull, or dropped as out-of-bounds -- are removed from the output. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bOutputSites"))
	bool bPruneOpenSites = true;

	/** Flag sites belonging to an open cell with a boolean attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bOutputSites && !bPruneOpenSites"))
	FName OpenSiteFlag = "OpenSite";

	/** Additional data to output for each Voronoi site. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Additional Outputs", meta = (PCG_Overridable, EditCondition="bOutputSites", ShowOnlyInnerProperties))
	FPCGExVoronoiSitesOutputDetails SitesOutputDetails;

private:
	friend class FPCGExBuildVoronoiGraph2DElement;
};

struct FPCGExBuildVoronoiGraph2DContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExBuildVoronoiGraph2DElement;

	TSharedPtr<PCGExData::FPointIOCollection> SitesOutput;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExBuildVoronoiGraph2DElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(BuildVoronoiGraph2D)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExBuildVoronoiGraph2D
{
	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExBuildVoronoiGraph2DContext, UPCGExBuildVoronoiGraph2DSettings>
	{
	protected:
		FPCGExGeo2DProjectionDetails ProjectionDetails;

		TArray<int8> WithinBounds;
		TBitArray<> IsVtxValid;

		TSharedPtr<TArray<int32>> OutputIndices;
		TSharedPtr<PCGExMath::Geo::TVoronoi2> Voronoi;
		TSharedPtr<PCGExGraphs::FGraphBuilder> GraphBuilder;

		TSharedPtr<PCGExData::FFacade> SiteDataFacade;
		TSharedPtr<PCGExData::TBuffer<bool>> OpenSiteWriter;

		FPCGExVoronoiSitesOutputDetails SitesOutputDetails;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;

		virtual void CompleteWork() override;
		virtual void Write() override;
		virtual void Output() override;

	protected:
		/** Build cluster vtx/edges and site outputs from the computed Voronoi diagram (all metrics) */
		bool BuildOutputs();
	};
}
