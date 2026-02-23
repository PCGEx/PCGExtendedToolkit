// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Collections/PCGExActorCollection.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Helpers/PCGExPCGGenerationWatcher.h"
#include "PCGManagedResource.h"
#include "PCGCrc.h"

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
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points spawn an actor.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

	virtual bool IsCacheable() const override { return false; }

public:
	// --- Spawning ---

	/** How to handle collisions when spawning actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning", meta=(PCG_Overridable))
	ESpawnActorCollisionHandlingMethod CollisionHandling = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// --- Tagging ---

	/** If enabled, apply collection entry tags to spawned actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable))
	bool bApplyEntryTags = false;

	/** Attribute forwarding from input points to output points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Tagging", meta=(PCG_Overridable))
	FPCGExForwardDetails TargetsForwarding;

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
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	PCGEX_CAN_ONLY_EXECUTE_ON_MAIN_THREAD(true)
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

		/** Pre-sized to NumPoints — each parallel thread writes to its own index, no locks */
		TArray<FResolvedEntry> ResolvedEntries;

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

#if WITH_EDITOR
		/** Cached folder path for organizing spawned actors */
		FName CachedFolderPath;
#endif

	public:
		explicit FProcessor(const TSharedRef<PCGExData::FFacade>& InPointDataFacade)
			: TProcessor(InPointDataFacade)
		{
		}

		virtual ~FProcessor() override = default;

		virtual bool Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager) override;
		virtual void ProcessPoints(const PCGExMT::FScope& Scope) override;
		virtual void OnPointsProcessingComplete() override;

	private:
		void SpawnAtPoint(int32 PointIndex);

#if WITH_EDITOR
		void ComputeFolderPath();
#endif
	};
}
