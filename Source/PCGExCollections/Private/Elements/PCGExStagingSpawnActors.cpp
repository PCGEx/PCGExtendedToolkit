// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExStagingSpawnActors.h"

#include "PCGComponent.h"
#include "PCGElement.h"
#include "PCGManagedResource.h"
#include "PCGParamData.h"
#include "Core/PCGExMT.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataMacros.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "Helpers/PCGExActorPropertyOverrides.h"
#include "Helpers/PCGExFunctionPrototypes.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Helpers/PCGHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExStagingSpawnActorsElement"
#define PCGEX_NAMESPACE StagingSpawnActors

PCGEX_INITIALIZE_ELEMENT(StagingSpawnActors)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingSpawnActors)

#pragma region UPCGExStagingSpawnActorsSettings

#if WITH_EDITOR

void UPCGExStagingSpawnActorsSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	// The override pins of the removed TargetsForwarding setting.
	RetireInputPin(InOutNode, FName("bEnabled"));
	RetireInputPin(InOutNode, FName("bPreserveAttributesDefaultValue"));
	RetireInputPin(InOutNode, FName("CommaSeparatedNames"));

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

#endif

void UPCGExStagingSpawnActorsSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
	PCGEX_PIN_PARAMS(PCGExCollections::Labels::SourceCollectionMapLabel, "Collection map information from, or merged from, Staging nodes.", Required)
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
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(StagingSpawnActors)

	PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->ActorReferenceAttribute)
	if (Settings->bApplyInstanceTags)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->InstanceTagsAttributeName)
	}

	Context->CollectionUnpacker = MakeShared<PCGExCollections::FPickUnpacker>();
	Context->CollectionUnpacker->UnpackPin(InContext, PCGExCollections::Labels::SourceCollectionMapLabel);

	if (!Context->CollectionUnpacker->HasValidMapping())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not rebuild a valid asset mapping from the provided map."));
		return false;
	}

	if (!Settings->PropertyOverrideDescriptions.IsEmpty())
	{
		Context->OverrideTargets = MakeShared<PCGExActorOverrides::FOverrideTargets>(
			Context, Settings->PropertyOverrideDescriptions, Settings->bQuietMissingMemberWarnings);
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
		// Compute CRC for managed resource reuse detection; each processor derives its own from it.
		GetDependenciesCrc(FPCGGetDependenciesCrcParams(&Context->InputData, Settings, nullptr), Context->DependenciesCrc);

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	// Output drives a main-thread loop that spawns actors (World->SpawnActor / NewObject<UPCGManagedActors> /
	// FinalizeSpawnedActor) -- illegal during a package save or GC. Defer the whole output phase (re-tick).
	PCGEX_DEFER_IF_OBJECT_WORK_BLOCKED

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

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		EntryHashGetter = PointDataFacade->GetReadable<int64>(Settings->GetEntryIdxAttributeName(), PCGExData::EIOSide::In, true);
		if (!EntryHashGetter)
		{
			return false;
		}

		if (Settings->bApplyInstanceTags)
		{
			InstanceTagsGetter = PointDataFacade->GetReadable<FString>(Settings->InstanceTagsAttributeName, PCGExData::EIOSide::In, true);
		}

		// Create ActorReference writer
		ActorRefWriter = PointDataFacade->GetWritable<FSoftObjectPath>(Settings->ActorReferenceAttribute, FSoftObjectPath(), false, PCGExData::EBufferInit::New);

		// Init root-actor source. Constant-mode short-circuits per-point materialization.
		RootActorSV = Settings->RootActor.GetValueSetting();
		if (!RootActorSV->Init(PointDataFacade))
		{
			return false;
		}

		// Init PCG generation watcher if requested
		if (Settings->bTriggerPCGGeneration)
		{
			PCGExPCGInterop::FGenerationConfig GenConfig;
			GenConfig.GenerateOnLoadAction = Settings->GenerateOnLoadAction;
			GenConfig.GenerateOnDemandAction = Settings->GenerateOnDemandAction;
			GenConfig.GenerateAtRuntimeAction = Settings->GenerateAtRuntimeAction;

			GenerationWatcher = MakeShared<PCGExPCGInterop::FGenerationWatcher>(TaskManager, GenConfig, Context->GetMutableComponent());
			GenerationWatcher->Initialize();
		}

		// Pre-size resolved entries -- one slot per point, no locks needed during parallel write
		NumPoints = PointDataFacade->Source->GetNum(PCGExData::EIOSide::In);
		ResolvedEntries.SetNumZeroed(NumPoints);

		if (!RootActorSV->IsConstant())
		{
			RootActorPaths.SetNum(NumPoints);
		}

		// Hoist out of the parallel hot path; checked per-point inside ProcessPoints.
		bApplyDeltas = Settings->bApplyPropertyDeltas;

		if (Context->OverrideTargets)
		{
			ActorOverrides = MakeShared<PCGExActorOverrides::FActorPropertyOverrides>(Context->OverrideTargets.ToSharedRef(), PointDataFacade->Source->GetIn());
		}

		InputCrc = Context->DependenciesCrc;
		if (InputCrc.IsValid())
		{
			InputCrc.Combine(static_cast<uint32>(BatchIndex));
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		// Reserve hint of 8 paths per scope: actor class + a handful of delta-collateral assets
		// (typical: 1 static mesh + a couple of materials). Sets grow if needed.
		ScopedUniquePaths = MakeShared<PCGExMT::TScopedSet<FSoftObjectPath>>(Loops, 8);
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		// Materialize per-point root paths only when not constant. The constant path is read directly
		// from settings on the main thread.
		const bool bRootIsConstant = RootActorSV->IsConstant();
		PCGEX_SV_VIEW_COND(RootActorSV, !bRootIsConstant)

		// Per-scope local set. Writes during the loop don't touch other scopes; collapse on
		// OnPointsProcessingComplete merges everything in a single pass.
		TSet<FSoftObjectPath>& LocalPaths = ScopedUniquePaths->Get_Ref(Scope);

		int16 MaterialPick = 0;

		PCGEX_SCOPE_LOOP(Index)
		{
			if (!PointFilterCache[Index])
			{
				continue;
			}

			const uint64 Hash = EntryHashGetter->Read(Index);
			if (Hash == 0 || Hash == static_cast<uint64>(-1))
			{
				continue;
			}

			FPCGExEntryAccessResult Result = Context->CollectionUnpacker->ResolveEntry(Hash, MaterialPick);
			if (!Result.IsValid())
			{
				continue;
			}

			if (!Result.Entry->IsType(PCGExAssetCollection::TypeIds::Actor))
			{
				if (!Settings->bQuietInvalidEntryWarnings)
				{
					PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Collection entry is not an Actor entry. Skipping."));
				}
				continue;
			}

			const FPCGExActorCollectionEntry* ActorEntry = static_cast<const FPCGExActorCollectionEntry*>(Result.Entry);
			const FSoftObjectPath ActorClassPath = ActorEntry->Actor.ToSoftObjectPath();
			if (!ActorClassPath.IsValid())
			{
				continue;
			}

			// Write directly to our index -- no lock, each thread writes unique indices
			ResolvedEntries[Index].Entry = ActorEntry;

			// Delta collateral paths are skipped when deltas are disabled -- prefetching
			// them would be wasted IO since the apply path won't run.
			LocalPaths.Add(ActorClassPath);
			if (bApplyDeltas)
			{
				LocalPaths.Append(ActorEntry->DeltaCollateralPaths);
			}

			if (!bRootIsConstant)
			{
				RootActorPaths[Index] = PCGEX_SV_READ(RootActorSV, Index - Scope.Start);
			}
		}
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::OnPointsProcessingComplete);

		TSet<FSoftObjectPath> UniquePaths;
		ScopedUniquePaths->Collapse(UniquePaths);
		ScopedUniquePaths.Reset();

		if (UniquePaths.IsEmpty())
		{
			bIsProcessorValid = false;
			return;
		}

		if (TryReuseSpawnedActors())
		{
			return;
		}

		// Cache transforms for the spawn loop
		Transforms = PointDataFacade->Source->GetIn()->GetConstTransformValueRange();

		TArray<FSoftObjectPath> PathsToLoad = UniquePaths.Array();

		// Warm cache: completes synchronously on this thread (see LoadTracked). Keep-alive is context-owned.
		PCGExHelpers::LoadTracked(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE, PathsToLoad = MoveTemp(PathsToLoad)]() -> TArray<FSoftObjectPath>
			{
				PCGEX_ASYNC_THIS_RET({})
				return PathsToLoad;
			},
			[PCGEX_ASYNC_THIS_CAPTURE](const bool bSuccess)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::OnLoadComplete);

				PCGEX_ASYNC_THIS
				This->OnSpawnAssetsLoaded();
			});
	}

	bool FProcessor::TryReuseSpawnedActors()
	{
		if (!InputCrc.IsValid())
		{
			return false;
		}

		int32 NumResolved = 0;
		for (const FResolvedEntry& Resolved : ResolvedEntries)
		{
			NumResolved += Resolved.Entry ? 1 : 0;
		}

#if WITH_EDITOR
		const UPCGComponent* SourceComponent = ExecutionContext->GetComponent();
		const bool bIsPreview = SourceComponent && SourceComponent->IsInPreviewMode();
#endif

		// Same guards as the engine's spawner. A resource short of one actor per point (a spawn failed) can't be
		// mapped back onto the points; one left by an interrupted run never got its CRC.
		const UPCGManagedActors* Reused = PCGExManagedHelpers::TryReuseManagedResource<UPCGManagedActors>(
			ExecutionContext->GetMutableComponent(), InputCrc,
			[&](const UPCGManagedActors* Resource)
			{
#if WITH_EDITOR
				if (Resource->IsPreview() != bIsPreview)
				{
					return false;
				}
#endif
				return Resource->GetConstGeneratedActors().Num() == NumResolved;
			});

		if (!Reused)
		{
			return false;
		}

		const TArray<TSoftObjectPtr<AActor>>& Actors = Reused->GetConstGeneratedActors();
		int32 ActorIndex = 0;
		for (int32 i = 0; i < NumPoints; i++)
		{
			if (ResolvedEntries[i].Entry)
			{
				ActorRefWriter->SetValue(i, Actors[ActorIndex++].ToSoftObjectPath());
			}
		}

		return true;
	}

	void FProcessor::OnSpawnAssetsLoaded()
	{
		// Nothing to preload: the spawn loop binds the overrides to each class as it meets it.
		if (!ActorOverrides || !ActorOverrides->HasPreloadableSources())
		{
			StartSpawnLoop();
			return;
		}

		// What to preload depends on each entry's class, and resolving a soft class is game thread object work:
		// done here when this callback is already in a position to, on the next tick otherwise.
		if (IsInGameThread() && !PCGExMT::IsObjectWorkBlocked())
		{
			PrepareOverridePreload();
			return;
		}

		PrepareOverridesStep = MakeShared<PCGExMT::FTimeSlicedMainThreadLoop>(1);
		PrepareOverridesStep->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->PrepareOverridePreload();
		};

		PCGEX_ASYNC_HANDLE_CHKD_VOID(TaskManager, PrepareOverridesStep)
	}

	void FProcessor::PrepareOverridePreload()
	{
		TArray<int32> Sources;
		for (const FResolvedEntry& Resolved : ResolvedEntries)
		{
			if (!Resolved.Entry || EntryPreloadSources.Contains(Resolved.Entry))
			{
				continue;
			}

			Sources.Reset();
			if (const UClass* ActorClass = Resolved.Entry->Actor.Get())
			{
				ActorOverrides->PrepareClass(ActorClass, Sources);
			}

			for (const int32 Source : Sources)
			{
				OverridePreloadSources.AddUnique(Source);
			}

			EntryPreloadSources.Add(Resolved.Entry, Sources);
		}

		if (OverridePreloadSources.IsEmpty())
		{
			StartSpawnLoop();
			return;
		}

		StartParallelLoopForRange(NumPoints);
	}

	void FProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedOverridePaths = MakeShared<PCGExMT::TScopedSet<FSoftObjectPath>>(Loops, 0);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::DiscoverOverrideAssets);

		TSet<FSoftObjectPath>& LocalPaths = ScopedOverridePaths->Get_Ref(Scope);

		for (const int32 Source : OverridePreloadSources)
		{
			ActorOverrides->GatherPreloadPaths(
				Source, Scope.Start, Scope.Count,
				[&](const int32 PointIndex)
				{
					const TArray<int32>* EntrySources = EntryPreloadSources.Find(ResolvedEntries[PointIndex].Entry);
					return EntrySources && EntrySources->Contains(Source);
				},
				LocalPaths);
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		TSet<FSoftObjectPath> UniquePaths;
		ScopedOverridePaths->Collapse(UniquePaths);
		ScopedOverridePaths.Reset();

		TArray<FSoftObjectPath> PathsToLoad = UniquePaths.Array();

		// Success is not required: whatever is still unloaded gets loaded by the engine accessor as it writes.
		// An empty set completes right away.
		PCGExHelpers::LoadTracked(
			TaskManager,
			[PathsToLoad = MoveTemp(PathsToLoad)]() -> TArray<FSoftObjectPath>
			{
				return PathsToLoad;
			},
			[PCGEX_ASYNC_THIS_CAPTURE](const bool bSuccess)
			{
				PCGEX_ASYNC_THIS
				This->StartSpawnLoop();
			});
	}

	void FProcessor::StartSpawnLoop()
	{
		MainThreadLoop = MakeShared<PCGExMT::FTimeSlicedMainThreadLoop>(NumPoints);

		// Weak capture: the loop is owned by this processor, a strong one would keep both alive forever.
		MainThreadLoop->OnIterationCallback = [PCGEX_ASYNC_THIS_CAPTURE](const int32 Index, const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->SpawnAtPoint(Index);
			if (Index == This->NumPoints - 1)
			{
				This->OnAllPointsVisited();
			}
		};

		PCGEX_ASYNC_HANDLE_CHKD_VOID(TaskManager, MainThreadLoop)
	}

	void FProcessor::OnAllPointsVisited()
	{
		// Only now: a resource whose run got interrupted keeps no CRC, so it can never be taken for a complete one.
		if (ManagedActors && InputCrc.IsValid())
		{
			ManagedActors->SetCrc(InputCrc);
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}

	AActor* FProcessor::ResolveTargetActor(const int32 PointIndex)
	{
		const FSoftObjectPath& Path = RootActorSV->IsConstant() ? Settings->RootActor.Constant : RootActorPaths[PointIndex];
		return PCGExCollections::ResolveTargetActor(ExecutionContext, Path, RootActorResolveCache);
	}

	void FProcessor::SpawnAtPoint(const int32 PointIndex)
	{
		const FPCGExActorCollectionEntry* ActorEntry = ResolvedEntries[PointIndex].Entry;
		if (!ActorEntry)
		{
			return;
		}

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
			return;
		}

		UWorld* World = ExecutionContext->GetWorld();
		if (!World)
		{
			return;
		}

		AActor* TargetActor = ResolveTargetActor(PointIndex);
		if (!TargetActor)
		{
			if (!Settings->bQuietInvalidEntryWarnings)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext,
				           FText::Format(LOCTEXT("InvalidTargetActor", "No target actor available for point {0}; ensure either RootActor or the component's target actor is set."),
					           FText::AsNumber(PointIndex)));
			}
			return;
		}

		const FTransform& SpawnTransform = Transforms[PointIndex];

		const bool bHasDelta = Settings->bApplyPropertyDeltas
			&& ActorEntry->SerializedPropertyDelta.Num() > 0;

		const UPCGComponent* SourceComponent = ExecutionContext->GetComponent();
		const bool bIsPreviewActor = (SourceComponent && SourceComponent->IsInPreviewMode());

		AActor* SpawnedActor = nullptr;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::WorldSpawnActor);
			FActorSpawnParameters SpawnParams;
			SpawnParams.Template = Cast<AActor>(ActorClass->GetDefaultObject());
			SpawnParams.SpawnCollisionHandlingOverride = Settings->CollisionHandling;
			// Spawn into the target actor's level so the spawned actor is serialized with the
			// same .umap as its parent (mirrors UPCGActorHelpers::SpawnDefaultActor and lets
			// per-point root actors in sublevels keep their spawned children in those sublevels).
			SpawnParams.OverrideLevel = TargetActor->GetLevel();
			// Mark transient at runtime / in PIE / when the PCG component is in preview
			// mode, so preview/runtime-spawned actors don't accidentally persist to disk.
			// Mirrors UPCGActorHelpers::SpawnDefaultActor's flag handling.
			if (PCGHelpers::IsRuntimeOrPIE() || bIsPreviewActor)
			{
				SpawnParams.ObjectFlags |= RF_Transient | RF_NonPIEDuplicateTransient;
			}
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
			return;
		}

		// Written after SpawnActor, not before: SCS components only exist once construction ran, and it re-duplicates
		// templates over earlier edits. Construction scripts, and BeginPlay when the world has begun play, have
		// therefore already run on template values.
		bool bWroteProperties = bHasDelta;
		if (ActorOverrides)
		{
			// One scope for both writers: a component both touch re-registers once, and the fixups run once, last.
			PCGExActorDelta::FScopedActorWrite WriteScope(SpawnedActor);
			if (bHasDelta)
			{
				PCGExActorDelta::ApplyPropertyDelta(WriteScope, ActorEntry->SerializedPropertyDelta);
			}

			// After the delta, so a per-point value wins over the one baked in the collection.
			bWroteProperties |= ActorOverrides->Apply(WriteScope, PointIndex);
		}
		else if (bHasDelta)
		{
			PCGExActorDelta::ApplyPropertyDelta(SpawnedActor, ActorEntry->SerializedPropertyDelta);
		}

		// The PCG point stays the source of truth for the transform, whatever construction scripts or fixups did to
		// the root. Compared first: SetActorTransform warns about a non-movable root even with nothing to move.
		if (bWroteProperties && !SpawnedActor->GetActorTransform().Equals(SpawnTransform))
		{
			SpawnedActor->SetActorTransform(SpawnTransform);
		}

		const bool bTransientSpawn = PCGHelpers::IsRuntimeOrPIE() || bIsPreviewActor;

		// UE-62747: SpawnActor doesn't properly apply scale from the spawn transform
		SpawnedActor->SetActorRelativeScale3D(SpawnTransform.GetScale3D());

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::AttachToParent);
			PCGHelpers::AttachToParent(SpawnedActor, TargetActor, Settings->AttachOptions, ExecutionContext);
		}

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

		// Create and register managed resource on first successful spawn.
		// Registering immediately ensures that if the time-sliced loop is
		// interrupted by a graph regeneration, already-spawned actors are
		// tracked and will be cleaned up.
		if (!ManagedActors)
		{
			UPCGComponent* MutableSourceComponent = ExecutionContext->GetMutableComponent();
			ManagedActors = NewObject<UPCGManagedActors>(MutableSourceComponent);

#if WITH_EDITOR
			// Explicitly reflect the component's editing mode on the resource. Without this,
			// bIsPreview may not match the component state and tracked actors can be treated
			// as transient. UE's PCG spawn element does this at PCGSpawnActor.cpp:972.
			ManagedActors->SetIsPreview(bIsPreviewActor);
#endif

			MutableSourceComponent->AddToManagedResources(ManagedActors);
		}

		PCGExCollections::FinalizeSpawnedActor(SpawnedActor, ManagedActors, bTransientSpawn);

		// Last, once the actor is tracked: these run user code against its final state.
		if (!Settings->PostProcessFunctionNames.IsEmpty())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::StagingSpawnActors::PostProcessFunctions);
			for (UFunction* Function : GetPostProcessFunctions(ActorClass))
			{
				SpawnedActor->ProcessEvent(Function, nullptr);
			}
		}

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

	const TArray<UFunction*>& FProcessor::GetPostProcessFunctions(UClass* ActorClass)
	{
		FPostProcessFunctions& Cached = PostProcessFunctions.FindOrAdd(TWeakObjectPtr<const UClass>(ActorClass));
		if (Cached.bResolved && Cached.Layout.IsCurrent())
		{
			return Cached.Functions;
		}

		// Resolved once per class layout, so FindUserFunctions reports a missing or mismatched function once, not per actor.
		Cached.bResolved = true;
		Cached.Layout = PCGExHelpers::FStructLayoutStamp();
		Cached.Layout.Add(ActorClass);
		Cached.Functions = PCGExHelpers::FindUserFunctions(ActorClass, Settings->PostProcessFunctionNames, {UPCGExFunctionPrototypes::GetPrototypeWithNoParams()}, ExecutionContext);

		return Cached.Functions;
	}
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
