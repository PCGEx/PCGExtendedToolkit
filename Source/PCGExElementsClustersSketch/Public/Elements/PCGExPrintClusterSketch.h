// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Fitting/PCGExFitting.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExPrintClusterSketch.generated.h"

class UPCGExClusterSketch;
struct FPCGExClusterSketchPrintContext;

namespace PCGEx
{
	template <typename T>
	class TAssetLoader;
}

namespace PCGExData
{
	class FDataForwardHandler;
	class FPointIOCollection;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

/**
 * Prints a hand-authored Cluster Sketch onto target points: each distinct sketch is printed ONCE into a
 * shared root cluster, then duplicated onto every target referencing it.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(PCGExNodeLibraryDoc="clusters/generate/print-cluster-sketch"))
class UPCGExPrintClusterSketchSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(PrintClusterSketch, "Sketch : Print to Points", "Prints a Cluster Sketch asset onto each target point.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

	//~Begin UPCGExPointsProcessorSettings
public:
	virtual FName GetMainInputPin() const override
	{
		return PCGExCommon::Labels::SourceTargetsLabel;
	}

	virtual FName GetMainOutputPin() const override
	{
		return PCGExClusters::Labels::OutputVerticesLabel;
	}

	/** One targets data at a time: the per-target sketch lookup and the root cache are keyed on it. */
	virtual bool GetMainAcceptMultipleData() const override
	{
		return false;
	}

	//~End UPCGExPointsProcessorSettings

	/** Cluster Sketch to print. Constant, or a per-point attribute path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Sketch", AllowedClasses="/Script/PCGExElementsClustersSketch.PCGExClusterSketch"))
	FPCGExInputShorthandNameSoftObjectPath Sketch = FPCGExInputShorthandNameSoftObjectPath(FName("Sketch"));

	/** How each printed cluster inherits its target point's transform. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExTransformDetails TransformDetails;

	/** Cluster (Vtx + Edges) output settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Which target attributes to promote to tags on the printed clusters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding", meta = (PCG_Overridable))
	FPCGExAttributeToTagDetails TargetsAttributesToClusterTags;

	/** Which target attributes to forward onto the printed Vtx data. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding", meta = (PCG_Overridable))
	FPCGExForwardDetails TargetsForwarding;

	/** Quiet the warnings for targets with no/invalid sketch, and for sketch model issues. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_NotOverridable))
	bool bQuiet = false;

private:
	friend class FPCGExPrintClusterSketchElement;
};

struct FPCGExPrintClusterSketchContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExPrintClusterSketchElement;

	virtual void RegisterAssetDependencies() override;

	FPCGExGraphBuilderDetails GraphBuilderDetails;
	FPCGExTransformDetails TransformDetails;
	FPCGExAttributeToTagDetails TargetsAttributesToClusterTags;
	TSharedPtr<PCGExData::FDataForwardHandler> TargetsForwardHandler;

	TSharedPtr<PCGExData::FFacade> TargetsDataFacade;

	/** Attribute-driven sketch resolution; null when the sketch is a constant. */
	TSharedPtr<PCGEx::TAssetLoader<UPCGExClusterSketch>> SketchLoader;
	TObjectPtr<UPCGExClusterSketch> ConstantSketch;

	/** Distinct sketches actually referenced, and the per-target index into them (-1 = skip). */
	TArray<TObjectPtr<UPCGExClusterSketch>> UniqueSketches;
	TArray<int32> SketchIdx;

	/** One printed root per unique sketch, parallel to UniqueSketches. */
	TArray<TSharedPtr<PCGExGraphs::FGraphBuilder>> GraphBuilders;
	TArray<TSharedPtr<FPCGExClusterSketchPrintContext>> PrintContexts;

	/** Pinless scratch holding the printed roots; only the per-target copies are staged. */
	TSharedPtr<PCGExData::FPointIOCollection> RootVtx;

	TSharedPtr<PCGExData::FPointIOCollection> VtxChildCollection;
	TSharedPtr<PCGExData::FPointIOCollection> EdgeChildCollection;
};

class FPCGExPrintClusterSketchElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(PrintClusterSketch)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
