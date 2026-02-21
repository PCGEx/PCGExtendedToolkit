// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingSpawnActors.h"

#include "PCGComponent.h"
#include "PCGManagedResource.h"
#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Engine/World.h"
#include "Fitting/PCGExFittingVariations.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"

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

		// Create ActorReference writer
		ActorRefWriter = PointDataFacade->GetWritable<FSoftObjectPath>(Settings->ActorReferenceAttribute, FSoftObjectPath(), false, PCGExData::EBufferInit::New);

		// Init fitting
		FittingHandler.ScaleToFit = Settings->ScaleToFit;
		FittingHandler.Justification = Settings->Justification;
		if (!FittingHandler.Init(ExecutionContext, PointDataFacade)) { return false; }

		Variations = Settings->Variations;
		Variations.Init(Settings->Seed);

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

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		const UPCGBasePointData* InPointData = PointDataFacade->Source->GetIn();
		const TConstPCGValueRange<int32> Seeds = InPointData->GetConstSeedValueRange();

		int16 MaterialPick = 0;
		FRandomStream RandomSource;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index]) { continue; }

			const uint64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == static_cast<uint64>(-1)) { continue; }

			FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, MaterialPick);
			if (!Result.IsValid()) { continue; }

			// Must be an actor entry
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

			// Compute fitted spawn transform
			FTransform OutTransform = FTransform::Identity;
			FVector OutTranslation = FVector::ZeroVector;
			FBox OutBounds = ActorEntry->Staging.Bounds;
			const FPCGExFittingVariations& EntryVariations = ActorEntry->GetVariations(Result.Host);

			RandomSource.Initialize(PCGExRandomHelpers::GetSeed(Seeds[Index], Variations.Seed));

			if (Variations.bEnabledBefore)
			{
				FTransform LocalXForm = FTransform::Identity;
				Variations.Apply(RandomSource, LocalXForm, EntryVariations, EPCGExVariationMode::Before);
				FittingHandler.ComputeLocalTransform(Index, LocalXForm, OutTransform, OutBounds, OutTranslation);
			}
			else
			{
				FittingHandler.ComputeTransform(Index, OutTransform, OutBounds, OutTranslation);
			}

			if (Variations.bEnabledAfter)
			{
				Variations.Apply(RandomSource, OutTransform, EntryVariations, EPCGExVariationMode::After);
			}

			{
				FWriteScopeLock WriteLock(RequestLock);
				FActorSpawnRequest& Request = SpawnRequests.AddDefaulted_GetRef();
				Request.PointIndex = Index;
				Request.Transform = OutTransform;
				Request.Entry = ActorEntry;
				Request.Host = Result.Host;
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		if (SpawnRequests.IsEmpty())
		{
			bIsProcessorValid = false;
			return;
		}

		MainThreadLoop = MakeShared<PCGExMT::FTimeSlicedMainThreadLoop>(SpawnRequests.Num());
		MainThreadLoop->OnIterationCallback = [&](const int32 Index, const PCGExMT::FScope& Scope) { SpawnActor(Index); };

		PCGEX_ASYNC_HANDLE_CHKD_VOID(TaskManager, MainThreadLoop)
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

	void FProcessor::SpawnActor(const int32 RequestIndex)
	{
		// This runs on the game thread via FTimeSlicedMainThreadLoop

		FActorSpawnRequest& Request = SpawnRequests[RequestIndex];
		UPCGComponent* SourceComponent = ExecutionContext->GetMutableComponent();

		// On first iteration, set up managed resources
		if (RequestIndex == 0)
		{
#if WITH_EDITOR
			ComputeFolderPath();
#endif
			ManagedActors = NewObject<UPCGManagedActors>(SourceComponent);
		}

		// Load actor class synchronously (game thread = safe)
		PCGExHelpers::LoadBlocking_AnyThread(Request.Entry->Actor.ToSoftObjectPath());
		UClass* ActorClass = Request.Entry->Actor.Get();

		if (ActorClass)
		{
			UWorld* World = ExecutionContext->GetWorld();

			if (World)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.SpawnCollisionHandlingOverride = Settings->CollisionHandling;

				AActor* SpawnedActor = World->SpawnActor<AActor>(ActorClass, Request.Transform, SpawnParams);

				if (SpawnedActor)
				{
					// UE-62747: SpawnActor doesn't properly apply scale from the spawn transform
					SpawnedActor->SetActorRelativeScale3D(Request.Transform.GetScale3D());

#if WITH_EDITOR
					if (CachedFolderPath != NAME_None)
					{
						SpawnedActor->SetFolderPath(CachedFolderPath);
					}
#endif

					// Apply entry tags to the actor
					if (Settings->bApplyEntryTags)
					{
						for (const FName& Tag : Request.Entry->Tags)
						{
							SpawnedActor->Tags.AddUnique(Tag);
						}
					}

					// Track in managed resources
					ManagedActors->GetMutableGeneratedActors().Add(SpawnedActor);

					// Write actor reference to output
					ActorRefWriter->SetValue(Request.PointIndex, FSoftObjectPath(SpawnedActor));

					// Optionally trigger PCG generation
					if (GenerationWatcher && Request.Entry->bHasPCGComponent)
					{
						TInlineComponentArray<UPCGComponent*, 1> PCGComps;
						SpawnedActor->GetComponents(PCGComps);
						for (UPCGComponent* PCGComp : PCGComps)
						{
							GenerationWatcher->Watch(PCGComp);
						}
					}
				}
				else if (!Settings->bQuietInvalidEntryWarnings)
				{
					PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
						FText::Format(LOCTEXT("FailedToSpawnActor", "Failed to spawn actor '{0}' at point {1}"),
							FText::FromString(ActorClass->GetName()), FText::AsNumber(Request.PointIndex)));
				}
			}
		}
		else if (!Settings->bQuietInvalidEntryWarnings)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
				FText::Format(LOCTEXT("FailedToLoadActor", "Failed to load actor class for point {0}"),
					FText::AsNumber(Request.PointIndex)));
		}

		// Register managed actors with PCG after the last spawn
		if (RequestIndex == SpawnRequests.Num() - 1 && ManagedActors)
		{
			SourceComponent->AddToManagedResources(ManagedActors);
		}
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
