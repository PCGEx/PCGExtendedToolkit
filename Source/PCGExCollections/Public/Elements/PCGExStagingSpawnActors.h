// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "PCGCrc.h"
#include "PCGManagedResource.h"
#include "Collections/PCGExActorCollection.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExPCGGenerationWatcher.h"

#include "PCGExStagingSpawnActors.generated.h"

/**
 * Spawns actors at staged point locations using collection map entries.
 * Each point with a valid actor collection entry will spawn the referenced actor class
 * at the point's transform, with optional tagging and PCG generation triggering.
 * Transforms are consumed as-is from upstream staging nodes (fitting is their responsibility).
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc",
	meta=(Keywords = "spawn actor staged collection", PCGExNodeLibraryDoc="staging/staging-spawn-actors"))
class UPCGExStagingSpawnActorsSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(StagingSpawnActors, "Staging : Spawn Actors", "Spawns actors from staged collection entries.");

	virtual EPCGSettingsType GetType() const override
	{
		return EPCGSettingsType::Spawner;
	}

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling);
	}
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points spawn an actor.", PCGExFactories::PointFilters(), false)
	//~End UPCGSettings

	virtual bool IsCacheable() const override
	{
		return false;
	}

public:
	// --- Targeting ---

	/** Optional root actor that owns the spawned actors. Resolves per-point (constant, data-domain attribute, or
	 *  per-point attribute). When the resolved soft path is null, falls back to the PCG component's target actor.
	 *  Resolution is by ResolveObject only -- paths must point to live actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning", meta=(PCG_Overridable))
	FPCGExInputShorthandNameSoftObjectPath RootActor;

	/** Controls where spawned actors appear in the Outliner and how they are parented to the root actor.
	 *  Mirrors UE PCG SpawnActor's AttachOptions semantics. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning")
	EPCGAttachOptions AttachOptions = EPCGAttachOptions::InFolder;

	// --- Spawning ---

	/** How to handle collisions when spawning actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning", meta=(PCG_Overridable))
	ESpawnActorCollisionHandlingMethod CollisionHandling = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	/** If enabled, apply per-instance property deltas stored on actor collection entries.
	 *  Actors with deltas are spawned deferred to set properties before construction completes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning", meta=(PCG_Overridable))
	bool bApplyPropertyDeltas = true;

	// --- Tagging ---

	/** If enabled, apply collection entry tags to spawned actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable))
	bool bApplyEntryTags = true;

	/** Attribute forwarding from input points to output points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable))
	FPCGExForwardDetails TargetsForwarding;

	/** If enabled, apply per-instance tags from the named attribute to spawned actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bApplyInstanceTags = true;

	/** Attribute name that contains the per-instance tags string (comma-separated). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable, EditCondition="bApplyInstanceTags"))
	FName InstanceTagsAttributeName = FName("InstanceTags");

	// --- PCG Generation ---

	/** If enabled, trigger PCG generation on spawned actors that have PCG components. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|PCG Generation", meta=(PCG_Overridable))
	bool bTriggerPCGGeneration = false;

	/** How to deal with found components that have the trigger condition 'GenerateOnLoad'. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|PCG Generation", meta=(PCG_Overridable, DisplayName="Grab GenerateOnLoad", EditCondition="bTriggerPCGGeneration", EditConditionHides))
	EPCGExGenerationTriggerAction GenerateOnLoadAction = EPCGExGenerationTriggerAction::Generate;

	/** How to deal with found components that have the trigger condition 'GenerateOnDemand'. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|PCG Generation", meta=(PCG_Overridable, DisplayName="Grab GenerateOnDemand", EditCondition="bTriggerPCGGeneration", EditConditionHides))
	EPCGExGenerationTriggerAction GenerateOnDemandAction = EPCGExGenerationTriggerAction::Generate;

	/** How to deal with found components that have the trigger condition 'GenerateAtRuntime'. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|PCG Generation", meta=(PCG_Overridable, DisplayName="Grab GenerateAtRuntime", EditCondition="bTriggerPCGGeneration", EditConditionHides))
	EPCGExRuntimeGenerationTriggerAction GenerateAtRuntimeAction = EPCGExRuntimeGenerationTriggerAction::AsIs;

	// --- Output ---

	/** Name of the attribute to write the spawned actor reference to. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Output", meta=(PCG_Overridable))
	FName ActorReferenceAttribute = FName("ActorReference");

	// --- Warnings ---

	/** Suppress warnings for invalid collection entries. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietInvalidEntryWarnings = false;
};

struct FPCGExStagingSpawnActorsContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingSpawnActorsElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionUnpacker;

	FPCGCrc DependenciesCrc;
	UPCGManagedActors* ReusedManagedActors = nullptr;

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExStagingSpawnActorsElement final : public FPCGExPointsProcessorElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override
	{
		return false;
	}

protected:
	PCGEX_ELEMENT_MAIN_THREAD_ONLY(true)
	PCGEX_ELEMENT_CREATE_CONTEXT(StagingSpawnActors)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExStagingSpawnActors
{
	/** Per-point resolved data, written lock-free during parallel phase (one slot per point index) */
	struct FResolvedEntry
	{
		const FPCGExActorCollectionEntry* Entry = nullptr;
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExStagingSpawnActorsContext, UPCGExStagingSpawnActorsSettings>
	{
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;
		TSharedPtr<PCGExData::TBuffer<FString>> InstanceTagsGetter;

		/** Per-point root actor source -- supports constant, data-domain, or per-point attribute */
		TSharedPtr<PCGExDetails::TSettingValue<FSoftObjectPath>> RootActorSV;

		/** Per-point soft paths, only allocated when not in Constant mode */
		TArray<FSoftObjectPath> RootActorPaths;

		/** Main-thread-only cache that dedups soft-path -> AActor resolution across the spawn loop */
		TMap<FSoftObjectPath, TWeakObjectPtr<AActor>> RootActorResolveCache;

		/** Pre-sized to NumPoints -- each parallel thread writes to its own index, no locks */
		TArray<FResolvedEntry> ResolvedEntries;

		/** Per-loop-scope dedup set for the soft paths we need pre-loaded before spawn.
		 *  Populated in parallel during ProcessPoints (each scope writes its own set, no
		 *  contention), collapsed on OnPointsProcessingComplete. Carries both the per-entry
		 *  actor class path AND the delta collateral paths (when bApplyPropertyDeltas) so a
		 *  single async batch load covers everything spawning needs. */
		TSharedPtr<PCGExMT::TScopedSet<FSoftObjectPath>> ScopedUniquePaths;

		/** Cached at Process time. Read inside the hot ProcessPoints loop to decide whether
		 *  to enqueue delta collateral paths alongside the actor class path. */
		bool bApplyDeltas = false;

		/** Main thread loop for spawning */
		TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;

		/** Keeps loaded actor classes alive */
		TSharedPtr<FStreamableHandle> LoadHandle;

		/** Managed resource for actor cleanup via PCG's native resource tracking */
		UPCGManagedActors* ManagedActors = nullptr;

		/** Optional PCG generation watcher */
		TSharedPtr<PCGExPCGInterop::FGenerationWatcher> GenerationWatcher;

		/** Output: actor reference writer */
		TSharedPtr<PCGExData::TBuffer<FSoftObjectPath>> ActorRefWriter;

		/** Forwarding handler */
		TSharedPtr<PCGExData::FDataForwardHandler> ForwardHandler;

		/** Cached transform range for main thread spawn loop */
		TConstPCGValueRange<FTransform> Transforms;
		int32 NumPoints = 0;

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override = default;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

	private:
		void SpawnAtPoint(int32 PointIndex);

		/** Resolve the per-point target actor: soft path -> ResolveObject -> fall back to component target */
		AActor* ResolveTargetActor(int32 PointIndex);
	};
}
