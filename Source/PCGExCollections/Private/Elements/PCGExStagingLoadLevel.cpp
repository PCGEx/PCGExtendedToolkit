// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadLevel.h"

#include <atomic>

#include "PCGComponent.h"
#include "PCGElement.h"
#include "PCGParamData.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "PCGExStagingLoadLevelElement"
#define PCGEX_NAMESPACE StagingLoadLevel

PCGEX_INITIALIZE_ELEMENT(StagingLoadLevel)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingLoadLevel)

#pragma region UPCGExManagedStreamingLevels

bool UPCGExManagedStreamingLevels::Release(bool bHardRelease, TSet<TSoftObjectPtr<AActor>>& OutActorsToDelete)
{
	for (const TWeakObjectPtr<ULevelStreamingDynamic>& WeakLevel : StreamingLevels)
	{
		if (ULevelStreamingDynamic* Level = WeakLevel.Get())
		{
			Level->SetIsRequestingUnloadAndRemoval(true);
		}
	}

	StreamingLevels.Reset();
	return true;
}

#pragma endregion

#pragma region UPCGExLevelStreamingDynamic

void UPCGExLevelStreamingDynamic::OnLevelLoadedChanged(ULevel* Level)
{
	Super::OnLevelLoadedChanged(Level);

	if (!Level) { return; }

	for (AActor* Actor : Level->Actors)
	{
		if (!Actor) { continue; }

		if (Actor->bIsMainWorldOnly)
		{
			Actor->Destroy();
			continue;
		}

#if WITH_EDITOR
		if (GeneratedFolderPath != NAME_None)
		{
			Actor->SetFolderPath(GeneratedFolderPath);
		}
#endif
	}
}

#pragma endregion

#pragma region UPCGExLevelStreamingLevelInstance

void UPCGExLevelStreamingLevelInstance::OnLevelLoadedChanged(ULevel* Level)
{
	Super::OnLevelLoadedChanged(Level);

	if (!Level) { return; }

#if WITH_EDITOR
	// Read folder path from the owning level instance actor
	FName GeneratedFolder = NAME_None;
	if (ILevelInstanceInterface* LevelInstance = GetLevelInstance())
	{
		if (const APCGExLevelInstance* OwnerInstance = Cast<APCGExLevelInstance>(Cast<AActor>(LevelInstance)))
		{
			GeneratedFolder = OwnerInstance->GeneratedFolderPath;
		}
	}
#endif

	for (AActor* Actor : Level->Actors)
	{
		if (!Actor) { continue; }

		if (Actor->bIsMainWorldOnly)
		{
			Actor->Destroy();
			continue;
		}

#if WITH_EDITOR
		if (GeneratedFolder != NAME_None)
		{
			Actor->SetFolderPath(GeneratedFolder);
		}
#endif
	}
}

#pragma endregion

#pragma region APCGExLevelInstance

TSubclassOf<ULevelStreamingLevelInstance> APCGExLevelInstance::GetLevelStreamingClass() const
{
	return UPCGExLevelStreamingLevelInstance::StaticClass();
}

#pragma endregion

#pragma region UPCGExStagingLoadLevelSettings

void UPCGExStagingLoadLevelSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

#pragma endregion

#pragma region FPCGExStagingLoadLevelElement

bool FPCGExStagingLoadLevelElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadLevel)

	Context->CollectionPickUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionPickUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionPickUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	return true;
}

bool FPCGExStagingLoadLevelElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingLoadLevelElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingLoadLevel)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		// Compute CRC for managed resource reuse detection
		GetDependenciesCrc(FPCGGetDependenciesCrcParams(&Context->InputData, Settings, nullptr), Context->DependenciesCrc);

		if (Context->DependenciesCrc.IsValid())
		{
			UPCGComponent* SourceComponent = Context->GetMutableComponent();

			// Try streaming levels first, then LevelInstance actors
			if (PCGExManagedHelpers::TryReuseManagedResource<UPCGExManagedStreamingLevels>(SourceComponent, Context->DependenciesCrc))
			{
				Context->bReusedManagedResources = true;
			}
#if WITH_EDITOR
			else if (PCGExManagedHelpers::TryReuseManagedResource<UPCGManagedActors>(SourceComponent, Context->DependenciesCrc))
			{
				Context->bReusedManagedResources = true;
			}
#endif
		}

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry) { return true; },
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	return Context->TryComplete();
}

#pragma endregion

#pragma region PCGExStagingLoadLevel::FProcessor

namespace PCGExStagingLoadLevel
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingLoadLevel::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Forward)

		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter) { return false; }

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingLoadLevel::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		UWorld* World = ExecutionContext->GetWorld();
		if (!World) { return; }

		TConstPCGValueRange<FTransform> Transforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();

		int16 MaterialPick = 0;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			const uint64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == static_cast<uint64>(-1)) { continue; }

			FPCGExEntryAccessResult Result = Context->CollectionPickUnpacker->ResolveEntry(Hash, MaterialPick);
			if (!Result.IsValid()) { continue; }

			const FSoftObjectPath& LevelPath = Result.Entry->Staging.Path;
			if (!LevelPath.IsValid()) { continue; }

			{
				// TODO : Move to TScopedArray instead
				FWriteScopeLock WriteLock(RequestLock);
				SpawnRequests.Emplace(World, LevelPath.GetLongPackageName(), LevelPath, Transforms[Index], Index);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		// All parallel work is done. Set up a main-thread loop to spawn level instances.
		// FTimeSlicedMainThreadLoop ensures spawning happens on the game thread.

		// TODO : Collapse SpawnRequests TScopedArray here

		// CRC reuse: managed resources from a previous execution match, skip spawning entirely
		if (Context->bReusedManagedResources) { return; }

		if (SpawnRequests.IsEmpty())
		{
			bIsProcessorValid = false;
			return;
		}

		// Monotonic generation counter for unique streaming level package names
		// Prevents name collisions with levels pending async unload from previous cycles
		static std::atomic<uint32> GenerationCounter{0};
		Generation = GenerationCounter.fetch_add(1);

		MainThreadLoop = MakeShared<PCGExMT::FTimeSlicedMainThreadLoop>(SpawnRequests.Num());
		MainThreadLoop->OnIterationCallback = [&](const int32 Index, const PCGExMT::FScope& Scope) { SpawnLevelInstance(Index); };

		PCGEX_ASYNC_HANDLE_CHKD_VOID(TaskManager, MainThreadLoop)
	}

#if WITH_EDITOR
	void FProcessor::ComputeFolderPath()
	{
		// Match native PCG convention: {OwnerFolder}/{OwnerLabel}_Generated
		const UPCGComponent* Component = ExecutionContext->GetComponent();
		if (!Component) { return; }

		const AActor* Owner = Component->GetOwner();
		if (!Owner) { return; }

		TStringBuilderWithBuffer<TCHAR, 1024> FolderBuilder;

		const FName OwnerFolder = Owner->GetFolderPath();
		if (OwnerFolder != NAME_None)
		{
			FolderBuilder << OwnerFolder.ToString() << TEXT("/");
		}

		FolderBuilder << Owner->GetActorNameOrLabel() << TEXT("_Generated");
		CachedFolderPath = FName(FolderBuilder.ToString());
	}

	void FProcessor::SpawnAsLevelInstance(FLevelSpawnRequest& Request)
	{
		UWorld* World = Request.Params.World;
		if (!World) { return; }

		// Resolve actor class — user override or our default
		UClass* ActorClass = Settings->LevelInstanceClass.Get();
		if (!ActorClass) { ActorClass = APCGExLevelInstance::StaticClass(); }

		// Defer construction so we can set WorldAsset BEFORE PostRegisterAllComponents.
		// The registration flow (PostRegisterAllComponents → RegisterLevelInstance → LoadLevelInstance)
		// only triggers loading if WorldAsset is already valid at that point.
		FActorSpawnParameters SpawnParams;
		SpawnParams.bDeferConstruction = true;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ALevelInstance* LevelInstanceActor = World->SpawnActor<ALevelInstance>(
			ActorClass,
			Request.Params.LevelTransform,
			SpawnParams);

		if (!LevelInstanceActor)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
			           FText::Format(LOCTEXT("FailedToSpawnLevelInstance", "Failed to spawn ALevelInstance for '{0}' at point {1}"),
				           FText::FromString(Request.Params.LongPackageName), FText::AsNumber(Request.PointIndex)));
			return;
		}

		// Pass folder path to our subclass so its streaming level can apply it
		if (APCGExLevelInstance* PCGExInstance = Cast<APCGExLevelInstance>(LevelInstanceActor))
		{
			PCGExInstance->GeneratedFolderPath = CachedFolderPath;
		}

		// Set world asset BEFORE finishing construction
		// FinishSpawning → PostRegisterAllComponents → RegisterLevelInstance → LoadLevelInstance
		// will see a valid WorldAsset and actually trigger the level streaming.
		const TSoftObjectPtr<UWorld> WorldAsset(Request.LevelPath);
		LevelInstanceActor->SetWorldAsset(WorldAsset);

		// Organize in folder
		if (CachedFolderPath != NAME_None)
		{
			LevelInstanceActor->SetFolderPath(CachedFolderPath);
		}

		// Finish spawning -- registers components
		LevelInstanceActor->FinishSpawning(Request.Params.LevelTransform);

		// Trigger level loading via the same path the editor uses when WorldAsset changes
		// (PostRegisterAllComponents has a GUID check that doesn't fire for editor-spawned actors)
		LevelInstanceActor->UpdateLevelInstanceFromWorldAsset();

		// Track via PCG managed resources -- engine handles cleanup on re-execution
		if (ManagedLevelInstances)
		{
			ManagedLevelInstances->GetMutableGeneratedActors().Add(LevelInstanceActor);
		}
	}
#endif

	void FProcessor::SpawnLevelInstance(const int32 RequestIndex)
	{
		// This runs on the game thread via FTimeSlicedMainThreadLoop

		FLevelSpawnRequest& Request = SpawnRequests[RequestIndex];

		const FString& BaseSuffix = Settings->LevelNameSuffix;

		UPCGComponent* SourceComponent = ExecutionContext->GetMutableComponent();

		// On first iteration, create managed resources for PCG cleanup tracking
		if (RequestIndex == 0)
		{
#if WITH_EDITOR
			// Compute folder path once (game thread, safe to access actor labels)
			ComputeFolderPath();

			// ALevelInstance actors persist across save/load — skip for runtime components
			// whose output is transient and would otherwise leave stale actors in the level.
			bUseLevelInstance = Settings->bSpawnAsLevelInstance
				&& SourceComponent->GenerationTrigger != EPCGComponentGenerationTrigger::GenerateAtRuntime;

			if (Settings->bSpawnAsLevelInstance && !bUseLevelInstance && !Settings->bQuietRuntimeFallbackWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
				           LOCTEXT("RuntimeFallback", "Spawn As Level Instance is enabled but the component uses Generate At Runtime. Falling back to streaming levels."));
			}

			if (bUseLevelInstance)
			{
				ManagedLevelInstances = NewObject<UPCGManagedActors>(SourceComponent);
			}
			else
#endif
			{
				ManagedStreamingLevels = NewObject<UPCGExManagedStreamingLevels>(SourceComponent);
			}
		}

#if WITH_EDITOR
		if (bUseLevelInstance)
		{
			SpawnAsLevelInstance(Request);

			// Register managed actors with PCG after the last spawn
			if (RequestIndex == SpawnRequests.Num() - 1 && ManagedLevelInstances)
			{
				ManagedLevelInstances->SetCrc(Context->DependenciesCrc);
				SourceComponent->AddToManagedResources(ManagedLevelInstances);
			}

			return;
		}
#endif

		const FString InstanceSuffix = FString::Printf(TEXT("%s_%u_%d"), *BaseSuffix, Generation, Request.PointIndex);
		Request.Params.OptionalLevelNameOverride = &InstanceSuffix;

		// Use our subclass that destroys bIsMainWorldOnly actors when the level finishes loading
		// (LoadLevelInstance doesn't go through World Partition, so engine won't filter them)
		UClass* StreamingClass = Settings->StreamingLevelClass.Get();
		Request.Params.OptionalLevelStreamingClass = StreamingClass ? StreamingClass : UPCGExLevelStreamingDynamic::StaticClass();

		bool bOutSuccess = false;
		ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstance(Request.Params, bOutSuccess);

		if (!bOutSuccess || !StreamingLevel)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
			           FText::Format(LOCTEXT("FailedToLoadLevel", "Failed to load level instance '{0}' at point {1}"),
				           FText::FromString(Request.Params.LongPackageName), FText::AsNumber(Request.PointIndex)));
			return;
		}

		if (UPCGExLevelStreamingDynamic* PCGExStreaming = Cast<UPCGExLevelStreamingDynamic>(StreamingLevel))
		{
			PCGExStreaming->OwnerSuffix = BaseSuffix;
#if WITH_EDITOR
			PCGExStreaming->GeneratedFolderPath = CachedFolderPath;
#endif
		}

		// Track via PCG managed resources -- PCG handles cleanup on re-execution
		if (ManagedStreamingLevels)
		{
			ManagedStreamingLevels->StreamingLevels.Add(StreamingLevel);
		}

		// Register managed streaming levels with PCG after the last spawn
		if (RequestIndex == SpawnRequests.Num() - 1 && ManagedStreamingLevels)
		{
			ManagedStreamingLevels->SetCrc(Context->DependenciesCrc);
			SourceComponent->AddToManagedResources(ManagedStreamingLevels);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
