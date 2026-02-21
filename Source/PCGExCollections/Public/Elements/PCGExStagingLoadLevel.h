// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExPointsProcessor.h"
#include "Core/PCGExPointFilter.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Engine/LevelStreamingDynamic.h"

#include "PCGExStagingLoadLevel.generated.h"

/**
 * Custom streaming level that enforces bIsMainWorldOnly filtering.
 * LoadLevelInstance doesn't go through World Partition, so bIsMainWorldOnly
 * actors slip through. This subclass destroys them when the level finishes loading.
 */
UCLASS()
class UPCGExLevelStreamingDynamic : public ULevelStreamingDynamic
{
	GENERATED_BODY()

protected:
	virtual void OnLevelLoadedChanged(ULevel* Level) override;
};

/**
 * Spawns level instances at staged point locations.
 * Each point with a valid level collection entry will spawn a streaming level instance
 * at the point's transform. Levels are spawned as ULevelStreamingDynamic instances
 * and tracked for cleanup on PCG regeneration.
 */
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Misc",
	meta=(Keywords = "spawn level instance staged world", PCGExNodeLibraryDoc="staging/staging-load-level"))
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
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	PCGEX_NODE_POINT_FILTER(PCGExFilters::Labels::SourcePointFiltersLabel, "Filters which points spawn a level instance.", PCGExFactories::PointFilters, false)
	//~End UPCGSettings

	virtual bool IsCacheable() const override { return false; }

public:
	/** Suffix appended to each spawned streaming level's package name to ensure uniqueness. If empty, uses point index. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FString LevelNameSuffix = TEXT("PCGEx");
};

struct FPCGExStagingLoadLevelContext final : FPCGExPointsProcessorContext
{
	friend class FPCGExStagingLoadLevelElement;

	TSharedPtr<PCGExCollections::FPickUnpacker> CollectionPickUnpacker;

	/** Tracking spawned streaming levels for cleanup on regeneration */
	TArray<TWeakObjectPtr<ULevelStreamingDynamic>> SpawnedStreamingLevels;

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
		ULevelStreamingDynamic::FLoadLevelInstanceParams Params;

		FLevelSpawnRequest(UWorld* InWorld, const FString& InPackageName, const FTransform& InTransform, const int32 InPointIndex)
			: PointIndex(InPointIndex)
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

		/** Main thread loop for spawning — runs on game thread via async handle */
		TSharedPtr<PCGExMT::FTimeSlicedMainThreadLoop> MainThreadLoop;

		/** Generation counter for unique level instance names */
		uint32 Generation = 0;

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
	};
}
