// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExElement.h"
#include "Elements/PCGActorSelector.h"
#include "Graphs/PCGExGraphDetails.h"
#include "Lattice/PCGExLatticeBasis.h"
#include "Sketch/PCGExClusterSketchModel.h"

#include "PCGExGetSketchData.generated.h"

class UPCGExClusterSketchDecorator;
struct FPCGExClusterSketchPrintContext;

namespace PCGExData
{
	class FPointIO;
	class FPointIOCollection;
}

namespace PCGExGraphs
{
	class FGraphBuilder;
}

/** Where the actors carrying Cluster Sketch Components come from. */
UENUM(BlueprintType)
enum class EPCGExSketchActorSource : uint8
{
	Selector = 0 UMETA(DisplayName = "Actor Selector", Tooltip="Find actors in the level with the standard PCG actor selector."),
	Input    = 1 UMETA(DisplayName = "Input References", Tooltip="Read actor references from an input pin -- an attribute set or point data."),
};

/**
 * Gathers Cluster Sketch Components off selected actors and prints each one as a cluster, placed by the
 * component's world transform so the output matches 1:1 what is authored and visible in the level.
 *
 * Shares the whole print path with the sketch asset and the component preview -- the only thing this
 * node adds is discovery and placement.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Clusters",
	meta = (Keywords = "pcgex sketch actor component getter cluster", PCGExNodeLibraryDoc = "clusters/generate/get-sketch-data"))
class UPCGExGetSketchDataSettings : public UPCGExSettings
{
	GENERATED_BODY()

	friend class FPCGExGetSketchDataElement;

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(GetSketchData, "Get Sketch Data", "Outputs clusters from Cluster Sketch Components found on selected actors, in world space.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spatial;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_NAME(ClusterGenerator);
	}

	virtual bool CanDynamicallyTrackKeys() const override
	{
		return true;
	}
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings

public:
	/** Where the actors come from. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExSketchActorSource ActorSource = EPCGExSketchActorSource::Selector;

	/** Standard PCG actor selection. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties, EditCondition = "ActorSource == EPCGExSketchActorSource::Selector", EditConditionHides))
	FPCGActorSelectorSettings ActorSelector;

	/** Attribute holding the actor reference, read from the References pin. Works on an attribute set or
	 *  point data alike -- both are read through the same accessor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "ActorSource == EPCGExSketchActorSource::Input", EditConditionHides))
	FPCGAttributePropertyInputSelector ActorReferenceAttribute;

	/** Narrows which components on the found actors are considered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (ShowOnlyInnerProperties))
	FPCGComponentSelectorSettings ComponentSelector;

	/** Skip components PCG itself spawned, mirroring the engine getters. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable), AdvancedDisplay)
	bool bIgnorePCGGeneratedComponents = true;

	/** Cluster (Vtx + Edges) output settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Cluster Output Settings"))
	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/** Write the source actor reference onto each output cluster as a @Data attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bWriteActorReference = true;

	/** Name of the '@Data' FSoftObjectPath attribute the source actor is written to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_Overridable, DisplayName = "Actor Reference", EditCondition = "bWriteActorReference"))
	FName ActorReferenceAttributeName = FName("ActorReference");

	/** Quiet the warnings for actors with no sketch component, and for sketch model issues. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_NotOverridable))
	bool bQuiet = false;
};

struct FPCGExGetSketchDataContext final : FPCGExContext
{
	friend class FPCGExGetSketchDataElement;

	FPCGExGraphBuilderDetails GraphBuilderDetails;

	/**
	 * One discovered component, snapshotted on the GAME THREAD in Boot. The model is copied rather than
	 * referenced so the print, which runs off-thread, never reads a live component.
	 */
	struct FSketchSource
	{
		FPCGExClusterSketchModel Model;
		FPCGExLatticeBasis Basis;
		bool bHasBasis = false;
		TArray<TObjectPtr<UPCGExClusterSketchDecorator>> Decorators;
		FTransform LocalToWorld = FTransform::Identity;
		FSoftObjectPath ActorPath;
	};

	TArray<FSketchSource> Sources;

	/** One printed cluster per source, parallel to Sources. */
	TArray<TSharedPtr<PCGExGraphs::FGraphBuilder>> GraphBuilders;
	TArray<TSharedPtr<FPCGExClusterSketchPrintContext>> PrintContexts;
	TArray<TSharedPtr<PCGExData::FPointIO>> VtxIOs;

	TSharedPtr<PCGExData::FPointIOCollection> VtxCollection;
	TSharedPtr<PCGExData::FPointIOCollection> EdgeCollection;
};

class FPCGExGetSketchDataElement final : public IPCGExElement
{
protected:
	PCGEX_ELEMENT_CREATE_CONTEXT(GetSketchData)

	// Boot walks actors and components, which is game-thread-only. The print and compile that follow
	// read the Boot snapshot, so they stay off-thread.
	PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE()

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
