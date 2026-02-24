// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingSpawnActors.h"

#include "PCGComponent.h"
#include "PCGElement.h"
#include "PCGManagedResource.h"
#include "PCGParamData.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Engine/World.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Helpers/PCGExActorPropertyDelta.h"

#define LOCTEXT_NAMESPACE "PCGExStagingSpawnActorsElement"
#define PCGEX_NAMESPACE StagingSpawnActors

PCGEX_INITIALIZE_ELEMENT(StagingSpawnActors)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingSpawnActors)

#pragma region UPCGExStagingSpawnActorsSettings

void UPCGExStagingSpawnActorsSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAM(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
	Super::InputPinPropertiesBeforeFilters(PinProperties);
}

TArray<FPCGPinProperties> UPCGExStagingSpawnActorsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	return PinProperties;
}

#pragma endregion

#pragma region FPCGExStagingSpawnActorsElement

bool FPCGExStagingSpawnActorsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(StagingSpawnActors)

	PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->ActorReferenceAttribute)

	Context->CollectionUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	return true;
}

bool FPCGExStagingSpawnActorsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExStagingSpawnActorsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(StagingSpawnActors)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		// Compute CRC for managed resource reuse detection
		GetDependenciesCrc(FPCGGetDependenciesCrcParams(&Context->InputData, Settings, nullptr), Context->DependenciesCrc);

		if (Context->DependenciesCrc.IsValid())
		{
			Context->ReusedManagedActors = PCGExManagedHelpers::TryReuseManagedResource<UPCGManagedActors>(
				Context->GetMutableComponent(), Context->DependenciesCrc);
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

#pragma region PCGExStagingSpawnActors::FProcessor

namespace PCGExStagingSpawnActors
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExStagingSpawnActors::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
		if (!EntryHashGetter) { return false; }

		if (Settings->bApplyInstanceTags)
		{
			InstanceTagsGetter = PointDataFacade->GetReadable<FString>(TEXT("InstanceTags"), PCGExData::EIOSide::In, true);
		}

		// Create ActorReference writer
		ActorRefWriter = PointDataFacade->GetWritable<FSoftObjectPath>(Settings->ActorReferenceAttribute, FSoftObjectPath(), false, PCGExData::EBufferInit::New);

		// Init forwarding
		ForwardHandler = Settings->TargetsForwarding.TryGetHandler(PointDataFacade);

		// Init PCG generation watcher if requested
		if (Settings->bTriggerPCGGeneration)
		{
			PCGExPCGInterop::FGenerationConfig GenConfig;
			GenConfig.GenerateOnLoadAction = Settings->GenerateOnLoadAction;
			GenConfig.GenerateOnDemandAction = Settings->GenerateOnDemandAction;
			GenConfig.GenerateAtRuntimeAction = Settings->GenerateAtRuntimeAction;

			GenerationWatcher = MakeShared<PCGExPCGInterop::FGenerationWatcher>(TaskManager, GenConfig);
			GenerationWatcher->Initialize();
		}

		// Pre-size resolved entries — one slot per point, no locks needed during parallel write
		NumPoints = PointDataFacade->Source->GetNum(PCGExData::EIOSide::In);
		ResolvedEntries.SetNumZeroed(NumPoints);

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		int16 MaterialPick = 0;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			const uint64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == static_cast<uint64>(-1)) { continue; }

			FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, MaterialPick);
			if (!Result.IsValid()) { continue; }

			if (Result.Host->GetTypeId() != PCGExAssetCollection::TypeIds::Actor)
			{
				if (!Settings->bQuietInvalidEntryWarnings)
				{
					PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Collection entry is not an Actor entry. Skipping."));
				}
				continue;
			}

			const FPCGExActorCollectionEntry* ActorEntry = static_cast<const FPCGExActorCollectionEntry*>(Result.Entry);
			if (!ActorEntry->Actor.ToSoftObjectPath().IsValid()) { continue; }

			// Write directly to our index — no lock, each thread writes unique indices
			ResolvedEntries[Index].Entry = ActorEntry;
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::OnPointsProcessingComplete);

		// Collect unique actor class paths from resolved entries
		TSet<FSoftObjectPath> UniqueClasses;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::CollectUniqueClasses);
			for (const FResolvedEntry& Resolved : ResolvedEntries)
			{
				if (Resolved.Entry)
				{
					UniqueClasses.Add(Resolved.Entry->Actor.ToSoftObjectPath());
				}
			}
		}

		if (UniqueClasses.IsEmpty())
		{
			bIsProcessorValid = false;
			return;
		}

		// CRC reuse: if managed actors from a previous execution match, skip spawning entirely
		if (Context->ReusedManagedActors)
		{
			const TArray<TSoftObjectPtr<AActor>>& Actors = Context->ReusedManagedActors->GetConstGeneratedActors();
			int32 ActorIdx = 0;
			for (int32 i = 0; i < NumPoints; ++i)
			{
				if (ResolvedEntries[i].Entry && ActorIdx < Actors.Num())
				{
					ActorRefWriter->SetValue(i, Actors[ActorIdx].ToSoftObjectPath());
					++ActorIdx;
				}
			}
			return;
		}

		// Cache transforms for the spawn loop
		Transforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();

#if WITH_EDITOR
		ComputeFolderPath();
#endif

		// Batch-load all unique actor classes asynchronously, then start spawning
		TArray<FSoftObjectPath> PathsToLoad = UniqueClasses.Array();

		PCGExHelpers::Load(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE, PathsToLoad = MoveTemp(PathsToLoad)]() -> TArray<FSoftObjectPath>
			{
				PCGEX_ASYNC_THIS_RET({})
				return PathsToLoad;
			},
			[PCGEX_ASYNC_THIS_CAPTURE](const bool bSuccess, TSharedPtr<FStreamableHandle> StreamableHandle)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::OnLoadComplete);

				PCGEX_ASYNC_THIS

				This->LoadHandle = StreamableHandle;

				This->MainThreadLoop = MakeShared<PCGExMT::FTimeSlicedMainThreadLoop>(This->NumPoints);
				This->MainThreadLoop->OnIterationCallback = [This](const int32 Index, const PCGExMT::FScope& Scope) { This->SpawnAtPoint(Index); };

				PCGEX_ASYNC_HANDLE_CHKD_VOID(This->TaskManager, This->MainThreadLoop)
			});
	}

#if WITH_EDITOR
	void FProcessor::ComputeFolderPath()
	{
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
#endif

	void FProcessor::SpawnAtPoint(const int32 PointIndex)
	{
		const bool bIsLastPoint = PointIndex == NumPoints - 1;
		const FPCGExActorCollectionEntry* ActorEntry = ResolvedEntries[PointIndex].Entry;

		if (!ActorEntry) { goto Finalize; }

		{
			// Class is already pre-loaded in OnPointsProcessingComplete
			UClass* ActorClass = ActorEntry->Actor.Get();

			if (!ActorClass)
			{
				if (!Settings->bQuietInvalidEntryWarnings)
				{
					PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
						FText::Format(LOCTEXT("FailedToLoadActor", "Failed to load actor class for point {0}"),
							FText::AsNumber(PointIndex)));
				}
				goto Finalize;
			}

			UWorld* World = ExecutionContext->GetWorld();
			if (!World) { goto Finalize; }

			const FTransform& SpawnTransform = Transforms[PointIndex];

			const bool bHasDelta = Settings->bApplyPropertyDeltas
				&& ActorEntry->SerializedPropertyDelta.Num() > 0;

			AActor* SpawnedActor = nullptr;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::WorldSpawnActor);
				FActorSpawnParameters SpawnParams;
				SpawnParams.Template = Cast<AActor>(ActorClass->GetDefaultObject());
				SpawnParams.SpawnCollisionHandlingOverride = Settings->CollisionHandling;
				if (bHasDelta) { SpawnParams.bDeferConstruction = true; }
				SpawnedActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform, SpawnParams);
			}

			if (!SpawnedActor)
			{
				if (!Settings->bQuietInvalidEntryWarnings)
				{
					PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
						FText::Format(LOCTEXT("FailedToSpawnActor", "Failed to spawn actor '{0}' at point {1}"),
							FText::FromString(ActorClass->GetName()), FText::AsNumber(PointIndex)));
				}
				goto Finalize;
			}

			// Apply property delta BEFORE finishing construction
			if (bHasDelta)
			{
				PCGExActorDelta::ApplyPropertyDelta(SpawnedActor, ActorEntry->SerializedPropertyDelta);
				SpawnedActor->FinishSpawning(SpawnTransform);
			}

			// UE-62747: SpawnActor doesn't properly apply scale from the spawn transform
			SpawnedActor->SetActorRelativeScale3D(SpawnTransform.GetScale3D());

			// Lazily create managed resource on first successful spawn
			if (!ManagedActors)
			{
				ManagedActors = NewObject<UPCGManagedActors>(ExecutionContext->GetMutableComponent());
			}

#if WITH_EDITOR
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::SetFolderPath);
				if (CachedFolderPath != NAME_None)
				{
					SpawnedActor->SetFolderPath(CachedFolderPath);
				}
			}
#endif

			// Apply entry tags to the actor
			if (Settings->bApplyEntryTags)
			{
				for (const FName& Tag : ActorEntry->Tags)
				{
					SpawnedActor->Tags.AddUnique(Tag);
				}
			}

			// Apply per-instance tags from InstanceTags attribute
			if (Settings->bApplyInstanceTags && InstanceTagsGetter)
			{
				const FString TagStr = InstanceTagsGetter->Read(PointIndex);
				if (!TagStr.IsEmpty())
				{
					TArray<FString> TagParts;
					TagStr.ParseIntoArray(TagParts, TEXT(","));
					for (const FString& Part : TagParts)
					{
						const FString Trimmed = Part.TrimStartAndEnd();
						if (!Trimmed.IsEmpty())
						{
							SpawnedActor->Tags.AddUnique(FName(*Trimmed));
						}
					}
				}
			}

			// Track in managed resources
			ManagedActors->GetMutableGeneratedActors().Add(SpawnedActor);

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::WriteActorRef);
				ActorRefWriter->SetValue(PointIndex, FSoftObjectPath(SpawnedActor));
			}

			// Optionally trigger PCG generation
			if (GenerationWatcher && ActorEntry->bHasPCGComponent)
			{
				TInlineComponentArray<UPCGComponent*, 1> PCGComps;
				SpawnedActor->GetComponents(PCGComps);
				for (UPCGComponent* PCGComp : PCGComps)
				{
					GenerationWatcher->Watch(PCGComp);
				}
			}
		}

	Finalize:
		// Register managed actors after the last point, regardless of whether it spawned
		if (bIsLastPoint && ManagedActors)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::RegisterManagedActors);
			ManagedActors->SetCrc(Context->DependenciesCrc);
			ExecutionContext->GetMutableComponent()->AddToManagedResources(ManagedActors);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
