// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingLoadLevel.h"

#include <atomic>

#include "PCGComponent.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "PCGExStagingLoadLevelElement"
#define PCGEX_NAMESPACE StagingLoadLevel

PCGEX_INITIALIZE_ELEMENT(StagingLoadLevel)
PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingLoadLevel)

#pragma region UPCGExLevelStreamingDynamic

void UPCGExLevelStreamingDynamic::OnLevelLoadedChanged(ULevel* Level)
{
	Super::OnLevelLoadedChanged(Level);

	if (!Level) { return; }

	for (AActor* Actor : Level->Actors)
	{
		if (Actor && Actor->bIsMainWorldOnly)
		{
			Actor->Destroy();
		}
	}
}

#pragma endregion

TArray<FPCGPinProperties> UPCGExStagingLoadLevelSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	return PinProperties;
}

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
				SpawnRequests.Emplace(World, LevelPath.GetLongPackageName(), Transforms[Index], Index);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		// All parallel work is done. Set up a main-thread loop to spawn level instances.
		// FTimeSlicedMainThreadLoop ensures spawning happens on the game thread.

		// TODO : Collapse SpawnRequests TScopedArray here
		
		
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

	void FProcessor::SpawnLevelInstance(const int32 RequestIndex)
	{
		// This runs on the game thread via FTimeSlicedMainThreadLoop

		FLevelSpawnRequest& Request = SpawnRequests[RequestIndex];

		const FString& BaseSuffix = Settings->LevelNameSuffix;

		// On first iteration, clean up previously spawned levels
		if (RequestIndex == 0)
		{
			UWorld* World = Request.Params.World;

			// Mark tracked levels for unload
			for (const TWeakObjectPtr<ULevelStreamingDynamic>& WeakLevel : Context->SpawnedStreamingLevels)
			{
				if (ULevelStreamingDynamic* OldLevel = WeakLevel.Get())
				{
					OldLevel->SetIsRequestingUnloadAndRemoval(true);
				}
			}
			Context->SpawnedStreamingLevels.Reset();

			// Scan for orphaned levels matching our suffix pattern
			TArray<ULevelStreaming*> OrphanedLevels;
			const FString SuffixPattern = BaseSuffix + TEXT("_");
			for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
			{
				if (!StreamingLevel) { continue; }
				if (StreamingLevel->GetWorldAssetPackageName().Contains(SuffixPattern))
				{
					OrphanedLevels.Add(StreamingLevel);
				}
			}

			for (ULevelStreaming* OrphanedLevel : OrphanedLevels)
			{
				OrphanedLevel->SetIsRequestingUnloadAndRemoval(true);
			}
		}

		const FString InstanceSuffix = FString::Printf(TEXT("%s_%u_%d"), *BaseSuffix, Generation, Request.PointIndex);
		Request.Params.OptionalLevelNameOverride = &InstanceSuffix;

		// Use our subclass that destroys bIsMainWorldOnly actors when the level finishes loading
		// (LoadLevelInstance doesn't go through World Partition, so engine won't filter them)
		Request.Params.OptionalLevelStreamingClass = UPCGExLevelStreamingDynamic::StaticClass();

		bool bOutSuccess = false;
		ULevelStreamingDynamic* StreamingLevel = ULevelStreamingDynamic::LoadLevelInstance(Request.Params, bOutSuccess);

		if (!bOutSuccess || !StreamingLevel)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
				FText::Format(LOCTEXT("FailedToLoadLevel", "Failed to load level instance '{0}' at point {1}"),
					FText::FromString(Request.Params.LongPackageName), FText::AsNumber(Request.PointIndex)));
			return;
		}

		Context->SpawnedStreamingLevels.Add(StreamingLevel);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
