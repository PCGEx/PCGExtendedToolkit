// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Engine/LevelStreamingDynamic.h"
#include "LevelInstance/LevelInstanceLevelStreaming.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "PCGManagedResource.h"
#include "PCGCrc.h"

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
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
	virtual FLinearColor GetNodeTitleColor() const override { return PCGEX_NODE_COLOR_OPTIN_NAME(Sampling); }
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	virtual void InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points spawn a level instance.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

	virtual bool IsCacheable() const override { return false; }

public:
	/** Suffix appended to each spawned streaming level's package name to ensure uniqueness. If empty, uses point index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString LevelNameSuffix = TEXT("PCGEx");

	/** Streaming level class used for the runtime (non-LevelInstance) path.
	 *  Override with a custom subclass for advanced streaming behavior.
	 *  If None, defaults to UPCGExLevelStreamingDynamic. */
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

protected:
	PCGEX_ELEMENT_BATCH_POINT_DECL
};

class FPCGExStagingLoadLevelElement final : public FPCGExPointsProcessorElement
{
public:
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }

protected:
	PCGEX_CAN_ONLY_EXECUTE_ON_MAIN_THREAD(true)
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
		ULevelStreamingDynamic::FLoadLevelInstanceParams Params;

		FLevelSpawnRequest(UWorld* InWorld, const FString& InPackageName, const FSoftObjectPath& InLevelPath, const FTransform& InTransform, const int32 InPointIndex)
			: PointIndex(InPointIndex)
			, LevelPath(InLevelPath)
			, Params(InWorld, InPackageName, InTransform)
		{
		}
	};

	class FProcessor final : public PCGExPointsMT::TProcessor<FPCGExStagingLoadLevelContext, UPCGExStagingLoadLevelSettings>
	{
		TSharedPtr<PCGExData::TBuffer<int64>> EntryHashGetter;

		/** Collected spawn requests from parallel phase */
		TArray<FLevelSpawnRequest> SpawnRequests;
		mutable FRWLock RequestLock;

		/** Main thread loop for spawning -- runs on game thread via async handle */
		TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;

		/** Generation counter for unique level instance names */
		uint32 Generation = 0;

		/** Managed resource for streaming level cleanup via PCG's native resource tracking */
		UPCGExManagedStreamingLevels* ManagedStreamingLevels = nullptr;

#if WITH_EDITOR
		/** Cached folder path for organizing spawned actors, computed on first spawn */
		FName CachedFolderPath;

		/** Resolved at first spawn: true if using ALevelInstance path, false for streaming.
		 *  Forced to false for runtime components since their output is transient. */
		bool bUseLevelInstance = false;

		/** Managed resource for ALevelInstance cleanup via PCG's native resource tracking */
		UPCGManagedActors* ManagedLevelInstances = nullptr;
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

#if WITH_EDITOR
		void ComputeFolderPath();
		void SpawnAsLevelInstance(FLevelSpawnRequest& Request);
#endif
	};
}
