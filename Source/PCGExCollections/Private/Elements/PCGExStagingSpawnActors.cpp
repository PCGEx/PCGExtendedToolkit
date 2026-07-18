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
#include "Data/Utils/PCGExDataForward.h"
#include "Details/PCGExSettingsDetails.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "Helpers/PCGExManagedResourceHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Helpers/PCGHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExStagingSpawnActorsElement"
#define PCGEX_NAMESPACE StagingSpawnActors

PCGEX_INITIALIZE_ELEMENT(StagingSpawnActors)

PCGEX_ELEMENT_BATCH_POINT_IMPL(StagingSpawnActors)

#pragma region UPCGExStagingSpawnActorsSettings

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

		EntryHashGetter = PointDataFacade->GetReadable<int64>(PCGExCollections::Labels::Tag_EntryIdx, PCGExData::EIOSide::In, true);
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

		// Init forwarding
		ForwardHandler = Settings->TargetsForwarding.TryGetHandler(PointDataFacade);

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

			if (Result.Entry->GetTypeId() != PCGExAssetCollection::TypeIds::Actor)
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

		TArray<FSoftObjectPath> PathsToLoad = UniquePaths.Array();

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
				This->MainThreadLoop->OnIterationCallback = [This](const int32 Index, const PCGExMT::FScope& Scope)
				{
					This->SpawnAtPoint(Index);
				};

				PCGEX_ASYNC_HANDLE_CHKD_VOID(This->TaskManager, This->MainThreadLoop)
			});
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

		// Apply delta AFTER SpawnActor has fully constructed the actor. Pre-construction apply
		// doesn't work: SCS components (anything added in the BP Components panel) don't exist
		// until ExecuteConstruction runs inside SpawnActor, and even if they did, SCS execution
		// re-duplicates templates over any prior edits. The only time all components are present
		// and stable is after SpawnActor returns.
		//
		// Known trade-off: the User Construction Script ran with template values, so logic that
		// consumes component state (e.g. "for each spline point, spawn a mesh") used defaults
		// rather than the delta-edited values. Preserving UCS visibility would require the
		// RerunConstructionScripts machinery -- out of scope here. The delta-edited state is
		// correct in the final actor; only the UCS pass is uninformed by it.
		if (bHasDelta)
		{
			PCGExActorDelta::ApplyPropertyDelta(SpawnedActor, ActorEntry->SerializedPropertyDelta);

			// Defensive re-apply of the spawn transform. The writer filters the root
			// component's relative-transform fields, so the delta itself shouldn't move the
			// actor -- but BP construction scripts or post-apply fixups occasionally touch
			// the root, and we want the PCG point to be the source of truth either way.
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
			ManagedActors->SetCrc(Context->DependenciesCrc);

#if WITH_EDITOR
			// Explicitly reflect the component's editing mode on the resource. Without this,
			// bIsPreview may not match the component state and tracked actors can be treated
			// as transient. UE's PCG spawn element does this at PCGSpawnActor.cpp:972.
			ManagedActors->SetIsPreview(bIsPreviewActor);
#endif

			MutableSourceComponent->AddToManagedResources(ManagedActors);
		}

		PCGExCollections::FinalizeSpawnedActor(SpawnedActor, ManagedActors, bTransientSpawn);

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
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
