// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCommon.h"
#include "PCGCrc.h"
#include "PCGManagedResource.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExPointsProcessor.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StreamableManager.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceLevelStreaming.h"

#include "PCGExStagingLoadLevel.generated.h"

/**
 * Custom streaming level that enforces bIsMainWorldOnly filtering
 * and organizes loaded actors into the PCG-generated folder.
 * LoadLevelInstance doesn't go through World Partition, so bIsMainWorldOnly
 * actors slip through. This subclass destroys them when the level finishes loading.
 */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExLevelStreamingDynamic : public ULevelStreamingDynamic
{
	GENERATED_BODY()

public:
	/** Suffix identifying which StagingLoadLevel node owns this streaming level */
	UPROPERTY()
	FString OwnerSuffix;

#if WITH_EDITORONLY_DATA
	/** Folder path to assign to loaded actors in the World Outliner */
	UPROPERTY()
	FName GeneratedFolderPath;
#endif

protected:
	virtual void OnLevelLoadedChanged(ULevel* Level) override;
};

/**
 * Custom streaming level for the ALevelInstance path.
 * Same bIsMainWorldOnly filtering + folder organization as UPCGExLevelStreamingDynamic,
 * but inherits from ULevelStreamingLevelInstance so it can be used with ALevelInstance::GetLevelStreamingClass().
 */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExLevelStreamingLevelInstance : public ULevelStreamingLevelInstance
{
	GENERATED_BODY()

protected:
	virtual void OnLevelLoadedChanged(ULevel* Level) override;
};

/**
 * Custom ALevelInstance subclass that routes streaming through our
 * UPCGExLevelStreamingLevelInstance class for bIsMainWorldOnly filtering.
 */
UCLASS()
class PCGEXCOLLECTIONS_API APCGExLevelInstance : public ALevelInstance
{
	GENERATED_BODY()

public:
#if WITH_EDITORONLY_DATA
	/** Folder path to assign to loaded actors in the World Outliner */
	FName GeneratedFolderPath;
#endif

	virtual TSubclassOf<ULevelStreamingLevelInstance> GetLevelStreamingClass() const override;
};

/**
 * Managed resource that tracks streaming levels spawned by StagingLoadLevel.
 * PCG's cleanup system calls Release() on re-execution, which unloads the levels.
 */
UCLASS()
class PCGEXCOLLECTIONS_API UPCGExManagedStreamingLevels : public UPCGManagedResource
{
	GENERATED_BODY()

public:
	virtual bool Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete) override;

	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> StreamingLevels;
};

/**
 * Spawns level instances at staged point locations.
 * Each point with a valid level collection entry will spawn a streaming level instance
 * at the point's transform. Levels are spawned as ULevelStreamingDynamic instances
 * and tracked for cleanup on PCG regeneration.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc",
	meta=(Keywords = "spawn level instance staged world", PCGExNodeLibraryDoc="staging/staging-spawn-level"))
class UPCGExStagingLoadLevelSettings : public UPCGExPointsProcessorSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(StagingLoadLevel, "Staging : Spawn Level", "Spawns level instances from staged points.");

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
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points spawn a level instance.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

	virtual bool IsCacheable() const override
	{
		return false;
	}

public:
	// --- Targeting ---

	/** Optional root actor that owns the spawned levels. Resolves per-point (constant, data-domain attribute,
	 *  or per-point attribute). When the resolved soft path is null, falls back to the PCG component's target
	 *  actor. Resolution is by ResolveObject only -- paths must point to live actors. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning", meta=(PCG_Overridable))
	FPCGExInputShorthandNameSoftObjectPath RootActor;

	/** Controls where spawned ALevelInstance actors appear in the Outliner and how they are parented to the
	 *  root actor. On the streaming-level path (no actor to attach), Attached silently behaves like InFolder. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Spawning")
	EPCGAttachOptions AttachOptions = EPCGAttachOptions::InFolder;

	/** Suffix appended to each spawned streaming level's package name to ensure uniqueness. If empty, uses point index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString LevelNameSuffix = TEXT("PCGEx");

	/** Streaming level class used for the runtime (non-LevelInstance) path.
	 *  Override with a custom subclass for advanced streaming behavior.
	 *  If None, defaults to UPCGExLevelStreamingDynamic.
	 *  WARNING: Do NOT use ULevelStreamingLevelInstance subclasses here -- they require the
	 *  ALevelInstance actor path and will not work with raw streaming level spawning. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	TSubclassOf<ULevelStreamingDynamic> StreamingLevelClass;

#if WITH_EDITORONLY_DATA
	/** When enabled (editor only), spawn ALevelInstance actors instead of raw streaming levels.
	 *  Gives proper inspector grouping with collapsible entries in the World Outliner.
	 *  Falls back to streaming if executed in cooked builds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bSpawnAsLevelInstance = false;

	/** Level instance actor class used for the ALevelInstance path.
	 *  Override with a custom subclass for advanced level instance behavior.
	 *  If None, defaults to APCGExLevelInstance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bSpawnAsLevelInstance"))
	TSubclassOf<ALevelInstance> LevelInstanceClass;
#endif

	/** Suppress the warning emitted when Spawn As Level Instance is enabled but the
	 *  component uses Generate At Runtime (which forces a fallback to streaming levels). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Warnings and Errors")
	bool bQuietRuntimeFallbackWarning = false;
};

struct FPCGExStagingLoadLevelContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingLoadLevelElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;

	FPCGCrc DependenciesCrc;
	bool bReusedManagedResources = false;

	/** Managed resource for streaming level cleanup via PCG's native resource tracking.
	 *  One per execute, shared across all processors. Created game-thread in AdvanceWork. */
	UPCGExManagedStreamingLevels* ManagedStreamingLevels = nullptr;

#if WITH_EDITOR
	/** True when using ALevelInstance path (bSpawnAsLevelInstance=true, editor, non-runtime). */
	bool bUseLevelInstance = false;

	/** True when spawning level content as individual persistent actors (bSpawnAsLevelInstance=false, editor, non-runtime). */
	bool bUseLooseActors = false;

	/** Managed resource for ALevelInstance cleanup. One per execute. */
	UPCGManagedActors* ManagedLevelInstances = nullptr;

	/** Managed resource for loose-actor cleanup. One per execute. */
	UPCGManagedActors* ManagedLooseActors = nullptr;
#endif

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExStagingLoadLevelElement final : public FPCGExPointsProcessorElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override
	{
		return false;
	}

protected:
	PCGEX_ELEMENT_MAIN_THREAD_ONLY(true)
	PCGEX_ELEMENT_CREATE_CONTEXT(StagingLoadLevel)

	virtual bool Boot(FPCGExContext* InContext) const override;
	virtual bool AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const override;
};

namespace PCGExStagingLoadLevel
{
	struct FLevelSpawnRequest
	{
		int32 PointIndex = -1;
		FSoftObjectPath LevelPath;
		FSoftObjectPath RootActorPath;
		ULevelStreamingDynamic::FLoadLevelInstanceParams Params;

		FLevelSpawnRequest(UWorld* InWorld, const FString& InPackageName, const FSoftObjectPath& InLevelPath, const FSoftObjectPath& InRootActorPath, const FTransform& InTransform, const int32 InPointIndex)
			: PointIndex(InPointIndex)
			  , LevelPath(InLevelPath)
			  , RootActorPath(InRootActorPath)
			  , Params(InWorld, InPackageName, InTransform)
		{
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExStagingLoadLevelContext, UPCGExStagingLoadLevelSettings>
	{
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;

		/** Per-point root actor source -- supports constant, data-domain, or per-point attribute */
		TSharedPtr<PCGExDetails::TSettingValue<FSoftObjectPath>> RootActorSV;

		/** Main-thread-only cache that dedups soft-path -> AActor resolution across the spawn loop */
		TMap<FSoftObjectPath, TWeakObjectPtr<AActor>> RootActorResolveCache;

		/** Collected spawn requests from parallel phase */
		TArray<FLevelSpawnRequest> SpawnRequests;
		mutable FRWLock RequestLock;

		/** Main thread loop for spawning -- runs on game thread via async handle */
		TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;

		/** Generation counter for unique level instance names */
		uint32 Generation = 0;

#if WITH_EDITOR
		/** Keeps source UWorld assets alive for the duration of the loose-actor spawn loop */
		TSharedPtr<FStreamableHandle> LevelLoadHandle;
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
		void SpawnLevelInstance(int32 RequestIndex);

		/** Resolve the per-request target actor: soft path -> ResolveObject -> fall back to component target */
		AActor* ResolveTargetActor(const FLevelSpawnRequest& Request);

		/** Compute the inner-actor folder for a streamed level. AttachOptions::Attached has no
		 *  meaningful semantics for inner actors (the host attach is handled separately), so it's
		 *  silently flattened to InFolder here. */
		FName ComputeInnerFolderPath(AActor* TargetActor) const;

#if WITH_EDITOR
		void SpawnAsLevelInstance(FLevelSpawnRequest& Request);
		void SpawnAsLooseActors(FLevelSpawnRequest& Request);
#endif
	};
}
