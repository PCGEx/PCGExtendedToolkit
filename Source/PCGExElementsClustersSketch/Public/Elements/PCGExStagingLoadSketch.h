// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExCollectionsCommon.h"
#include "Clusters/PCGExClusterCommon.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Fitting/PCGExFitting.h"
#include "Graphs/PCGExGraphDetails.h"

#include "PCGExStagingLoadSketch.generated.h"

class UPCGExClusterSketch;
struct FPCGExClusterSketchPrintContext;

namespace PCGEx
{
	template <typename T>
	class TAssetLoader;
}

/** Where the node gets the sketch to print for each target point. */
UENUM(BlueprintType)
enum class EPCGExClusterSketchSource : uint8
{
	CollectionMap = 0 UMETA(DisplayName = "Collection Map", Tooltip="Resolve each target's sketch from a staged pick, through a Collection Map. Requires points staged by a distribution node."),
	Asset         = 1 UMETA(DisplayName = "Asset", Tooltip="Resolve each target's sketch from a direct asset reference -- a constant, or a per-point path attribute."),
};

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
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Clusters", meta=(Keywords = "sketch staged spawn print cluster", PCGExNodeLibraryDoc="staging/staging-load-sketch"))
class UPCGExStagingLoadSketchSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	// Shortname feeds GetDefaultNodeName() and may diverge from the title (cf. PCGDataAssetLoader).
	PCGEX_NODE_INFOS(StagingLoadSketch, "Staging : Load Sketch", "Prints Cluster Sketch assets onto staged points.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif

	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

protected:
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
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

	//~End UPCGExPointsProcessorSettings

	/** Where each target's sketch comes from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExClusterSketchSource Source = EPCGExClusterSketchSource::CollectionMap;

	/** Staging layer this node reads staged picks from. None = default layer (PCGEx/CollectionEntry); otherwise the layer name is appended (PCGEx/CollectionEntry/<layer>). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition="Source == EPCGExClusterSketchSource::CollectionMap", EditConditionHides), AdvancedDisplay)
	FName StagingLayer = NAME_None;

	FName GetEntryIdxAttributeName() const
	{
		return PCGExCollections::Labels::EntryIdxName(StagingLayer);
	}

	/** Cluster Sketch to print. Constant, or a per-point attribute path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName="Cluster Sketch", EditCondition="Source == EPCGExClusterSketchSource::Asset", EditConditionHides, AllowedClasses="/Script/PCGExElementsClustersSketch.PCGExClusterSketch"))
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

	/** Which target attributes to forward onto the printed Vtx data. A forwarded name equal to an
	 *  authored sketch attribute REPLACES it on that duplicate -- forwarding recreates the attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Forwarding", meta = (PCG_Overridable))
	FPCGExForwardDetails TargetsForwarding;

	/** Quiet the warnings for targets with no/invalid sketch, and for sketch model issues. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_NotOverridable))
	bool bQuiet = false;

private:
	friend class FPCGExStagingLoadSketchElement;
};

struct FPCGExStagingLoadSketchContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingLoadSketchElement;

	virtual void RegisterAssetDependencies() override;

	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/**
	 * One entry per input data. FCopyGraphToPoint takes the details by RAW POINTER, so this array is
	 * sized once in Boot and never resized -- the addresses must outlive the copy tasks.
	 */
	struct FTargets
	{
		TSharedPtr<PCGExData::FFacade> Facade;
		FPCGExTransformDetails TransformDetails;
		FPCGExAttributeToTagDetails AttributesToClusterTags;
		TSharedPtr<PCGExData::FDataForwardHandler> ForwardHandler;

		/** Per-point index into UniqueSketches (-1 = skip). */
		TArray<int32> SketchIdx;
	};

	TArray<FTargets> Targets;

	/** Attribute-driven sketch resolution; null when the sketch is a constant. Asset source only. */
	TSharedPtr<PCGEx::TAssetLoader<UPCGExClusterSketch>> SketchLoader;
	TObjectPtr<UPCGExClusterSketch> ConstantSketch;

	/** CollectionMap source only: distinct sketch paths resolved from staged picks in Boot.
	 *  UniqueSketches is built parallel to this, so one index addresses both. */
	TArray<FSoftObjectPath> UniqueSketchPaths;

	/**
	 * Targets that referenced a sketch but could not get one -- broken pick, or an asset that failed
	 * to load. A target whose pick resolves to a NON-sketch entry is not counted: mixed hosting is the
	 * point of Omni, so a mesh pick flowing past this node is expected, not a fault.
	 */
	int32 NumUnresolvedTargets = 0;

	/** Distinct sketches referenced across ALL inputs -- two inputs naming the same sketch print one
	 *  shared root. In CollectionMap mode a slot is null when its path failed to load. */
	TArray<TObjectPtr<UPCGExClusterSketch>> UniqueSketches;

	/** One printed root per unique sketch, parallel to UniqueSketches. */
	TArray<TSharedPtr<PCGExGraphs::FGraphBuilder>> GraphBuilders;
	TArray<TSharedPtr<FPCGExClusterSketchPrintContext>> PrintContexts;

	/** Pinless scratch holding the printed roots; only the per-target copies are staged. */
	TSharedPtr<PCGExData::FPointIOCollection> RootVtx;

	TSharedPtr<PCGExData::FPointIOCollection> VtxChildCollection;
	TSharedPtr<PCGExData::FPointIOCollection> EdgeChildCollection;
};

class FPCGExStagingLoadSketchElement final : public FPCGExPointsProcessorElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(StagingLoadSketch)

	// Boot unpacks the Collection Map, whose cache-miss path marshals-and-waits on the game thread.
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()
	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual void PostLoadAssetsDependencies(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
