// Copyright 2026 Timothe Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "PCGCrc.h"
#include "Core/PCGExClipper2Processor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "PhysicsEngine/BodyInstance.h"

#include "PCGExClipper2Volume.generated.h"

class AActor;
class AVolume;
class UPCGManagedActors;

namespace PCGExDetails
{
	template <typename T>
	class TSettingValue;
}

// Defined in the .cpp -- plain build data produced by Process(Group), consumed by OutputWork() when spawning the volume.
struct FPCGExVolumeSpec;

/** How each convex piece's floor Z is derived from its own points. */
UENUM(BlueprintType)
enum class EPCGExVolumeBaseMode : uint8
{
	Flat    = 0 UMETA(DisplayName = "Flat", ToolTip = "All pieces share one flat floor at the footprint's lowest point."),
	Min     = 1 UMETA(DisplayName = "Min", ToolTip = "Each piece's floor sits at the lowest point Z within that piece (contains all of its points)."),
	Max     = 2 UMETA(DisplayName = "Max", ToolTip = "Each piece's floor sits at the highest point Z within that piece."),
	Average = 3 UMETA(DisplayName = "Average", ToolTip = "Each piece's floor sits at the average point Z within that piece."),
};

/** Output wrapper produced per footprint. */
UENUM(BlueprintType)
enum class EPCGExClipper2VolumeOutputMode : uint8
{
	Volume    = 0 UMETA(DisplayName = "Volume", ToolTip = "Spawn an AVolume per footprint and output UPCGVolumeData (original behavior)."),
	Primitive = 1 UMETA(DisplayName = "Primitive", ToolTip = "Spawn a simple static-mesh collider (convex simple collision) per footprint and output UPCGPrimitiveData -- for contexts where an AVolume isn't suitable."),
};

/** Clipper2 : Volume -- extrudes a concave closed-path footprint into an AVolume (or a simple static-mesh collider), writing each Hertel-Mehlhorn convex piece as a vertical prism in AggGeom.ConvexElems (no BSP, so editor + cooked runtime both work). */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Path", meta=(PCGExNodeLibraryDoc="utilities/clipper2-volume"))
class UPCGExClipper2VolumeSettings : public UPCGExClipper2ProcessorSettings
{
	GENERATED_BODY()

public:
	UPCGExClipper2VolumeSettings(const FObjectInitializer& ObjectInitializer);

	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Clipper2Volume, "Clipper2 : Volume", "Extrude a closed path footprint into an AVolume trigger volume.");

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return FLinearColor::White;
	}

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spawner;
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;

	virtual bool IsCacheable() const override
	{
		return false;
	}

	//~End UPCGSettings

public:
	/** Whether each footprint becomes an AVolume (UPCGVolumeData) or a simple static-mesh collider (UPCGPrimitiveData). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExClipper2VolumeOutputMode OutputMode = EPCGExClipper2VolumeOutputMode::Volume;

	/** Projection settings used to flatten the path into a 2D footprint and to orient the extrusion. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FPCGExGeo2DProjectionDetails ProjectionDetails;

	/** Fill rule used when triangulating the footprint. Even-Odd treats nested rings as holes (donut volumes). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExClipper2FillRule FillRule = EPCGExClipper2FillRule::EvenOdd;

	/** Volume actor class to spawn (Volume mode). Must derive from AVolume (e.g. ATriggerVolume, APostProcessVolume, a custom gameplay volume). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta = (PCG_NotOverridable, EditCondition = "OutputMode == EPCGExClipper2VolumeOutputMode::Volume", EditConditionHides))
	TSubclassOf<AVolume> VolumeClass;

	/** Actor class spawned as the collider host (Primitive mode). A UStaticMeshComponent carrying the convex simple collision is attached to it -- and made the root component when the chosen class has none. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Primitive", meta = (PCG_NotOverridable, EditCondition = "OutputMode == EPCGExClipper2VolumeOutputMode::Primitive", EditConditionHides))
	TSubclassOf<AActor> PrimitiveActorClass;

	/** Extrusion height applied above the footprint base plane. Constant, per-point attribute, or property (e.g. $Extent). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta = (PCG_Overridable))
	FPCGExInputShorthandSelectorDouble Height = FPCGExInputShorthandSelectorDouble(FName("Height"), 200.0);

	/** Minimum extrusion height. Pieces thinner than this are clamped up so the prism never collapses. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta = (PCG_Overridable, ClampMin = 0.01))
	double MinThickness = 1.0;

	/** How each convex piece's floor Z is chosen from its points, so the volume steps to follow uneven input (Flat keeps one shared floor). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Volume", meta = (PCG_NotOverridable))
	EPCGExVolumeBaseMode BaseMode = EPCGExVolumeBaseMode::Min;

	/** If enabled, greedily merge triangles into larger convex pieces (Hertel-Mehlhorn). Disable for raw per-triangle prisms. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Decomposition", meta = (PCG_NotOverridable))
	bool bMergeConvexPieces = true;

	/** Safety cap on convex pieces per volume. Volumes exceeding this are skipped with a warning (protects physics broadphase). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Decomposition", meta = (PCG_Overridable, ClampMin = 1))
	int32 MaxConvexPieces = 256;

	/** If enabled, override the spawned volume's collision profile with the one below (Volume mode). Otherwise the class default is kept. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Collision", meta = (PCG_NotOverridable, EditCondition = "OutputMode == EPCGExClipper2VolumeOutputMode::Volume", EditConditionHides))
	bool bOverrideCollisionProfile = false;

	/** Collision profile applied to the spawned volume's brush component when the override is enabled (Volume mode). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Collision", meta = (PCG_Overridable, EditCondition = "bOverrideCollisionProfile && OutputMode == EPCGExClipper2VolumeOutputMode::Volume", EditConditionHides))
	FName CollisionProfileName = FName("Trigger");

	/** Full collision setup (preset/object type/responses) applied to the spawned static-mesh collider (Primitive mode). Defaults to a blocking profile. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Collision", meta = (PCG_NotOverridable, EditCondition = "OutputMode == EPCGExClipper2VolumeOutputMode::Primitive", EditConditionHides))
	FBodyInstance CollisionBody;

	/** Controls where each spawned volume/primitive actor lands in the Outliner: in a folder named after the target
	 *  actor (default), attached to it, or loose at the root. The anchor is the PCG component's target actor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning", meta = (PCG_NotOverridable))
	EPCGAttachOptions AttachOptions = EPCGAttachOptions::InFolder;

	/** Name of the soft-object-path attribute written into each output volume's @Data domain (points at the spawned actor). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta = (PCG_NotOverridable))
	FName ActorReferenceAttributeName = FName("ActorReference");

	/** Suppress per-group warnings about degenerate footprints / failed triangulation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors", meta = (PCG_NotOverridable))
	bool bQuietTriangulationWarnings = false;

	/** Specify a list of functions to be called on the target actor after actor creation. Functions need to be parameter-less and with "CallInEditor" flag enabled. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, AdvancedDisplay)
	TArray<FName> PostProcessFunctionNames;
	
	virtual FPCGExGeo2DProjectionDetails GetProjectionDetails() const override;

	virtual bool SupportOpenMainPaths() const override
	{
		return false; // Volumes require closed footprints.
	}

	virtual bool SupportsAutoGrouping() const override
	{
		return true;
	} // outer + nested holes -> one volume
};

struct FPCGExClipper2VolumeContext final : FPCGExClipper2ProcessorContext
{
	friend class FPCGExClipper2VolumeElement;

	// Per-source-path height reader (Facade->Idx indexes this array, matching AllOpData).
	TArray<TSharedPtr<PCGExDetails::TSettingValue<double>>> HeightValues;

	// Volume build specs produced off-thread in Process(Group), consumed on the game thread in OutputWork.
	TArray<TSharedPtr<FPCGExVolumeSpec>> StagedVolumes;
	mutable FCriticalSection StagedVolumesLock;

	// Managed resource owning all spawned volumes for this generation (auto-cleanup on regen).
	UPCGManagedActors* ManagedActors = nullptr;

	// CRC of inputs + settings, stamped on the managed resource.
	FPCGCrc DependenciesCrc;

	void AddStagedVolume(const TSharedPtr<FPCGExVolumeSpec>& Spec);

	/** Spawns every staged volume. MUST run on the game thread -- OutputWork marshals it there. */
	void SpawnStagedVolumes();

	virtual void Process(const TSharedPtr<PCGExClipper2::FProcessingGroup>& Group) override;
};

class FPCGExClipper2VolumeElement final : public FPCGExClipper2ProcessorElement
{
protected:
	PCGEX_ELEMENT_MAIN_THREAD_ONLY(true) // OutputWork spawns actors + cooks collision on the game thread.
	PCGEX_ELEMENT_CREATE_CONTEXT(Clipper2Volume)

	virtual bool PostBoot(FPCGExContext* InContext) const override;
	virtual void OutputWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};
