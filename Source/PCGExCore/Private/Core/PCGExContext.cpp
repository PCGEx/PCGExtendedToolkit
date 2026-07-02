// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExContext.h"

#include "PCGComponent.h"
#include "PCGExCoreMacros.h"
#include "PCGExSubSystem.h"
#include "PCGManagedResource.h"
#include "Async/Async.h"
#include "Containers/PCGExManagedObjects.h"
#include "Core/PCGExElement.h"
#include "Core/PCGExMT.h"
#include "Core/PCGExSettings.h"
#include "Data/PCGExDataCommon.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExProxyData.h"
#include "Engine/AssetManager.h"
#include "Engine/EngineTypes.h"
#include "Factories/PCGExInstancedFactory.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGDynamicTrackingHelpers.h"
#include "Helpers/PCGExFunctionPrototypes.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Utils/PCGExUniqueNameGenerator.h"

#define LOCTEXT_NAMESPACE "PCGExContext"

UPCGExInstancedFactory* FPCGExContext::RegisterInstancedFactory(UPCGExInstancedFactory* BaseOperation, const FName OverridePinLabel)
{
	BaseOperation->BindContext(this); // Temp so Copy doesn't crash

	UPCGExInstancedFactory* RetValue = BaseOperation->CreateNewInstance(ManagedObjects.Get());
	if (!RetValue)
	{
		return nullptr;
	}
	InternalOperations.Add(RetValue);
	RetValue->InitializeInContext(this, OverridePinLabel);
	return RetValue;
}

#pragma region Output Data

void FPCGExContext::IncreaseStagedOutputReserve(const int32 InIncreaseNum)
{
	FWriteScopeLock WriteScopeLock(StagingLock);
	StagedData.Reserve(StagedData.Num() + InIncreaseNum);
}

void FPCGExContext::StageOutput(UPCGData* InData, const FName& InPin, const PCGExData::EStaging Staging, const TSet<FString>& InTags)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExContext::StageOutput);

	// Staging accumulates outputs during async processing. They're moved to
	// OutputData.TaggedData in OnComplete. The lock guards against concurrent
	// staging from parallel tasks and the race with OnComplete flushing.
	{
		FWriteScopeLock WriteScopeLock(StagingLock);

		if (IsWorkCancelled() || IsWorkCompleted())
		{
			return;
		}

		FPCGTaggedData& Output = StagedData.Emplace_GetRef();
		Output.Data = InData;
		Output.Pin = InPin;
		Output.Tags.Append(InTags);
		Output.bPinlessData = EnumHasAnyFlags(Staging, PCGExData::EStaging::Pinless);
	}

	// Managed: register with ManagedObjects so it survives GC until context teardown.
	if (EnumHasAnyFlags(Staging, PCGExData::EStaging::Managed))
	{
		ManagedObjects->Add(InData);
	}

	// Mutable: this is data we own and can modify. Clean up consumable attributes
	// (internal PCGEx attributes not meant for downstream nodes) unless they're protected.
	if (EnumHasAnyFlags(Staging, PCGExData::EStaging::Mutable))
	{
		if (bCleanupConsumableAttributes)
		{
			if (UPCGMetadata* Metadata = InData->MutableMetadata())
			{
				for (const FName ConsumableName : ConsumableAttributesSet)
				{
					if (Metadata->HasAttribute(ConsumableName)
						&& !ProtectedAttributesSet.Contains(ConsumableName))
					{
						Metadata->DeleteAttribute(ConsumableName);
					}
				}
			}
		}

		// Backstop for outputs created outside FPointIO::InitializeOutput / FManagedObjects::DuplicateData
		// (e.g. direct engine-side DuplicateData calls): materialize inherited @Data values locally before
		// this data leaves the node, while its ancestor chain is still alive. Idempotent -- attributes
		// that already carry a local entry are skipped. Runs after consumable cleanup so deleted
		// attributes aren't needlessly resolved.
		PCGExData::Helpers::LocalizeDataValues(InData);

		if (bFlattenOutput)
		{
			InData->Flatten();
		}
	}
}

#pragma endregion

UWorld* FPCGExContext::GetWorld() const
{
	return GetComponent()->GetWorld();
}

const UPCGComponent* FPCGExContext::GetComponent() const
{
	return Cast<UPCGComponent>(ExecutionSource.Get());
}

UPCGComponent* FPCGExContext::GetMutableComponent() const
{
	return Cast<UPCGComponent>(ExecutionSource.Get());
}

TSharedPtr<PCGExMT::FTaskManager> FPCGExContext::GetTaskManager()
{
	if (!TaskManager)
	{
		FWriteScopeLock WriteLock(AsyncLock);
		TaskManager = MakeShared<PCGExMT::FTaskManager>(this);
		TaskManager->OnEndCallback = [CtxHandle = GetWeakSelfHandle()](const bool bWasCancelled)
		{
			if (bWasCancelled)
			{
				return;
			}

			PCGEX_SHARED_CONTEXT_VOID(CtxHandle);

			FPCGExContext* Ctx = SharedContext.Get();

			if (Ctx && Ctx->ElementHandle)
			{
				Ctx->OnAsyncWorkEnd(bWasCancelled);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("OnEnd but no context or element handle!"))
			}
		};
	}

	return TaskManager;
}

void FPCGExContext::PauseContext()
{
	bIsPaused = true;
}

void FPCGExContext::UnpauseContext()
{
	bIsPaused = false;
}

bool FPCGExContext::IsRuntimeGen() const
{
	const UPCGComponent* Comp = GetComponent();
	return Comp && Comp->GenerationTrigger == EPCGComponentGenerationTrigger::GenerateAtRuntime;
}

FPCGExContext::FPCGExContext()
{
	// Create the PCG handle once, up front: the single source of truth for this context's lifetime,
	// read everywhere via GetWeakSelfHandle() so it is never resurrected after Release(). Must run
	// before ManagedObjects, which captures it.
	SelfHandle = GetOrCreateHandle();
	AsyncPinTracker = MakeShared<PCGExMT::FAsyncContextPinTracker>();

	WorkHandle = MakeShared<PCGEx::FWorkHandle>();
	ManagedObjects = MakeShared<PCGEx::FManagedObjects>(SelfHandle, WorkHandle);
	UniqueNameGenerator = MakeShared<FPCGExUniqueNameGenerator>();
	BufferProxyPool = MakeShared<PCGExData::IBufferProxyPool>();
}

FPCGExContext::~FPCGExContext()
{
	//WorkHandle.Reset();
	ManagedObjects->Flush(); // So cleanups can be recursively triggered while manager is still alive
	PCGExHelpers::SafeReleaseHandles(TrackedAssets);
	TrackedCachedAssets.Empty(); // wrappers self-release on drop (RAII); the subsystem cache may keep the asset warm
}

void FPCGExContext::ExecuteOnNotifyActors(const TArray<FName>& FunctionNames)
{
	// Execute PostProcess Functions
	if (!NotifyActors.IsEmpty())
	{
		if (IsInGameThread())
		{
			// FindUserFunctions does FindObject/StaticFindObjectFast -- illegal mid package-save or GC. This GT branch
			// can be reached via the marshal below being pumped during SavePackage's render flush; bail rather than crash.
			if (PCGExMT::IsObjectWorkBlocked()) { return; }

			TArray<AActor*> NotifyActorsArray = NotifyActors.Array();
			for (AActor* TargetActor : NotifyActorsArray)
			{
				if (!IsValid(TargetActor))
				{
					continue;
				}
				for (UFunction* Function : PCGExHelpers::FindUserFunctions(TargetActor->GetClass(), FunctionNames, {UPCGExFunctionPrototypes::GetPrototypeWithNoParams()}, this))
				{
					TargetActor->ProcessEvent(Function, nullptr);
				}
			}
		}
		else
		{
			PCGExMT::ExecuteOnMainThreadAndWait([&]()
			{
				ExecuteOnNotifyActors(FunctionNames);
			});
		}
	}
}

void FPCGExContext::AddExtraStructReferencedObjects(FReferenceCollector& Collector)
{
	FPCGContext::AddExtraStructReferencedObjects(Collector);
	ManagedObjects->AddExtraStructReferencedObjects(Collector);
}

void FPCGExContext::AddNotifyActor(AActor* InActor)
{
	if (IsValid(InActor))
	{
		FWriteScopeLock WriteScopeLock(NotifyActorsLock);
		NotifyActors.Add(InActor);
	}
}
#pragma region State

bool FPCGExContext::IsWaitingForTasks()
{
	if (TaskManager)
	{
		return TaskManager->IsWaitingForTasks();
	}
	return false;
}

void FPCGExContext::ReadyForExecution()
{
	UnpauseContext();
	SetState(PCGExCommon::States::State_InitialExecution);
}

void FPCGExContext::SetState(const PCGExCommon::ContextState StateId)
{
	CurrentState.store(StateId.GetComparisonIndex().ToUnstableInt(), std::memory_order_release);
}

void FPCGExContext::Done()
{
	SetState(PCGExCommon::States::State_Done);
}

bool FPCGExContext::DriveAdvanceWork(const UPCGExSettings* InSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExContext::DriveAdvanceWork)
	
	// This pattern short-circuits the PCG scheduler to avoid frame delays.
	// OnAsyncWorkEnd calls this directly so work continues immediately when async completes,
	// rather than waiting for PCG's next-frame scheduling. The compare_exchange ensures
	// only one caller drives at a time, with others setting bPendingAsyncWorkEnd for pickup.

	// Try to become the driver - only one caller can drive at a time
	bool bExpected = false;
	if (!bAdvanceWorkInProgress.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
	{
		// Someone else is driving - request they do another round when done
		bPendingAsyncWorkEnd.store(true, std::memory_order_release);
		return false;
	}

	// We're the driver - keep advancing until no more pending completions
	bool bResult;
	do
	{
		bPendingAsyncWorkEnd.store(false, std::memory_order_release);

		if (!CanExecute())
		{
			bResult = true;
			break;
		}

		bResult = ElementHandle->AdvanceWork(this, InSettings);
	}
	while (bPendingAsyncWorkEnd.load(std::memory_order_acquire));

	bAdvanceWorkInProgress.store(false, std::memory_order_release);

	// Final check: pending may have been set between the do-while check and clearing the flag
	if (bPendingAsyncWorkEnd.exchange(false, std::memory_order_acq_rel))
	{
		return DriveAdvanceWork(InSettings);
	}

	// Safety net: if AdvanceWork signaled "done" (true) but the context was never properly
	// finalized, cancel to prevent a hang. This catches bugs where AdvanceWork returns true
	// without calling TryComplete() or CancelExecution() -- the context would remain paused
	// and PCG would wait forever. We cancel rather than complete because outputs are likely
	// incomplete in this state.
	if (bResult && !IsWorkCompleted() && !IsWorkCancelled())
	{
		const FString NodeName = InSettings ? InSettings->GetName() : TEXT("Unknown");
		UE_LOG(LogTemp, Error, TEXT("[%s] AdvanceWork returned true without completing or cancelling. Forcing cancellation to prevent hang. Please report this at https://github.com/Nebukam/PCGExtendedToolkit/issues"), *NodeName);
		CancelExecution(FString::Printf(TEXT("[%s] AdvanceWork returned true without proper finalization. Please report this issue."), *NodeName));
	}

	return bResult;
}

bool FPCGExContext::TryComplete(const bool bForce)
{
	if (IsWorkCancelled() || IsWorkCompleted())
	{
		return true;
	}
	if (!bForce && !IsDone())
	{
		return false;
	}

	bool bExpected = false;
	if (bWorkCompleted.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
	{
		OnComplete();
	}

	return true;
}

void FPCGExContext::OnAsyncWorkEnd(const bool bWasCancelled)
{
	PCGEX_SHARED_CONTEXT_VOID(GetWeakSelfHandle());

	if (bWasCancelled || IsWorkCancelled())
	{
		// A cancel can land while async work is still in flight and the context is paused. CancelExecution
		// unpauses once, but a subsequent re-pause can consume that wake-up and strand the task in PCG's
		// PausedTasks forever. Unpause again here so PCG re-ticks and finalizes the cancelled task cleanly
		// (CanExecute() is false, so the re-tick just completes it - no double work).
		UnpauseContext();
		return;
	}

	// Use DriveAdvanceWork which handles all coordination
	// If someone else is driving, it will set bPendingAsyncWorkEnd for them to pick up
	const UPCGExSettings* Settings = GetInputSettings<UPCGExSettings>();
	switch (CurrentPhase)
	{
	case EPCGExecutionPhase::PrepareData:
		ElementHandle->AdvancePreparation(this, Settings);
		break;
	case EPCGExecutionPhase::Execute:
		DriveAdvanceWork(Settings);
		break;
	default:
		break;
	}
}

void FPCGExContext::OnComplete()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExContext::OnComplete);

	if (ElementHandle)
	{
		ElementHandle->CompleteWork(this);
	}

	PCGEX_TERMINATE_ASYNC

	{
		// Flush staged outputs into the PCG output data. Also remove them from
		// ManagedObjects since PCG now owns the data lifecycle.
		FWriteScopeLock WriteScopeLock(StagingLock);
		OutputData.TaggedData.Append(StagedData);
		ManagedObjects->Remove(StagedData);
		StagedData.Empty();
	}

	// Unpause allows the PCG scheduler to collect our outputs and mark the node complete.
	UnpauseContext();
}

#pragma endregion

#pragma region Async resource management

TSet<FSoftObjectPath>& FPCGExContext::GetRequiredAssets()
{
	FWriteScopeLock WriteScopeLock(AssetsLock);
	if (!RequiredAssets)
	{
		RequiredAssets = MakeShared<TSet<FSoftObjectPath>>();
	}
	return *RequiredAssets.Get();
}

void FPCGExContext::RegisterAssetDependencies()
{
}

void FPCGExContext::AddAssetDependency(const FSoftObjectPath& Dependency)
{
	FWriteScopeLock WriteScopeLock(AssetsLock);
	if (!RequiredAssets)
	{
		RequiredAssets = MakeShared<TSet<FSoftObjectPath>>();
	}
	RequiredAssets->Add(Dependency);
}

bool FPCGExContext::LoadAssets()
{
	bWarmDependencies = false;

	if (!RequiredAssets || RequiredAssets->IsEmpty())
	{
		return false;
	}

	SetState(PCGExCommon::States::State_LoadingAssetDependencies);

	// LoadTracked returns true when every dependency was already resident (warm cache) and the load
	// therefore completed synchronously in this call. Record that so AdvancePreparation can skip the
	// async yield, and so downstream PostLoadAssetsDependencies overrides can tell a warm re-run from
	// a cold load via bWarmDependencies.
	bWarmDependencies = PCGExHelpers::LoadTracked(
		GetTaskManager(),
		[CtxHandle = GetWeakSelfHandle()]() -> TArray<FSoftObjectPath>
		{
			PCGEX_SHARED_CONTEXT_RET(CtxHandle, {})
			return SharedContext.Get()->RequiredAssets->Array();
		}, [CtxHandle = GetWeakSelfHandle()](const bool bSuccess)
		{
			PCGEX_SHARED_CONTEXT_VOID(CtxHandle)
			if (!bSuccess)
			{
				SharedContext.Get()->CancelExecution("Error loading assets.");
			}
		});


	return true;
}


void FPCGExContext::TrackAssetsHandle(const TSharedPtr<FStreamableHandle>& InHandle)
{
	if (!InHandle.IsValid() || !InHandle->IsActive())
	{
		return;
	}
	{
		FWriteScopeLock WriteScopeLock(AssetsLock);
		TrackedAssets.Add(InHandle);
	}
}

void FPCGExContext::TrackCachedAsset(const TSharedPtr<PCGExHelpers::FPCGExSharedAssetHandle>& InHandle)
{
	if (!InHandle.IsValid())
	{
		return;
	}
	{
		FWriteScopeLock WriteScopeLock(AssetsLock);
		TrackedCachedAssets.Add(InHandle);
	}
}

UPCGManagedComponent* FPCGExContext::AttachManagedComponent(AActor* InParent, UActorComponent* InComponent, const FAttachmentTransformRules& AttachmentRules) const
{
	// Transfers a component from PCGEx internal management to PCG's managed resource system.
	// This ensures PCG can clean up the component on re-generate/destroy, while the component
	// remains tagged for PCG ownership (source component name + DefaultPCGTag).
	UPCGComponent* SrcComp = GetMutableComponent();

	const bool bIsPreviewMode = SrcComp->IsInPreviewMode();

	// Remove from internal management first. If it wasn't internally managed
	// (e.g. created externally), clear root/async flags that would prevent GC.
	if (!ManagedObjects->Remove(InComponent))
	{
		InComponent->RemoveFromRoot();
		InComponent->ClearInternalFlags(EInternalObjectFlags::Async);
	}

	InComponent->ComponentTags.Reserve(InComponent->ComponentTags.Num() + 2);
	InComponent->ComponentTags.Add(SrcComp->GetFName());
	InComponent->ComponentTags.Add(PCGHelpers::DefaultPCGTag);

	UPCGManagedComponent* ManagedComponent = NewObject<UPCGManagedComponent>(SrcComp);
	ManagedComponent->GeneratedComponent = InComponent;
	SrcComp->AddToManagedResources(ManagedComponent);

	if (IPCGExManagedComponentInterface* Managed = Cast<IPCGExManagedComponentInterface>(InComponent))
	{
		Managed->SetManagedComponent(ManagedComponent);
	}

	InParent->Modify(!bIsPreviewMode);

	InComponent->RegisterComponent();
	InParent->AddInstanceComponent(InComponent);

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(InComponent))
	{
		USceneComponent* Root = InParent->GetRootComponent();
		SceneComponent->SetMobility(Root->Mobility);
		SceneComponent->AttachToComponent(Root, AttachmentRules);
	}

	return ManagedComponent;
}

void FPCGExContext::AddConsumableAttributeName(const FName InName)
{
	{
		FReadScopeLock ReadScopeLock(ConsumableAttributesLock);
		if (ConsumableAttributesSet.Contains(InName))
		{
			return;
		}
	}
	{
		FWriteScopeLock WriteScopeLock(ConsumableAttributesLock);
		ConsumableAttributesSet.Add(InName);
	}
}

void FPCGExContext::AddProtectedAttributeName(const FName InName)
{
	{
		FReadScopeLock ReadScopeLock(ProtectedAttributesLock);
		if (ProtectedAttributesSet.Contains(InName))
		{
			return;
		}
	}
	{
		FWriteScopeLock WriteScopeLock(ProtectedAttributesLock);
		ProtectedAttributesSet.Add(InName);
	}
}

void FPCGExContext::EDITOR_TrackPath(const FSoftObjectPath& Path, const bool bIsCulled)
{
#if WITH_EDITOR
	FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(this, FPCGSelectionKey::CreateFromPath(Path), false);
#endif
}

void FPCGExContext::EDITOR_TrackClass(const TSubclassOf<UObject>& InSelectionClass, bool bIsCulled)
{
#if WITH_EDITOR
	FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(this, FPCGSelectionKey(InSelectionClass), false);
#endif
}

void FPCGExContext::EDITOR_TrackPCGComponentData(const UPCGComponent* InComponent, const bool bIsCulled)
{
#if WITH_EDITOR
	if (!InComponent) { return; }

	const AActor* Owner = InComponent->GetOwner();
	if (!Owner) { return; }

	// The UPCGComponent extra dependency is mandatory: without it the actor/component mapping filters out
	// the dirty when a change originates from a PCG component regen (PCGActorAndComponentMapping ClearCacheForKeys).
	FPCGSelectionKey Key = FPCGSelectionKey::CreateFromObjectPtr(Owner);
	Key.SetExtraDependency(UPCGComponent::StaticClass());
	FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(this, MoveTemp(Key), bIsCulled);
#endif
}

bool FPCGExContext::CanExecute() const
{
	return !InputData.bCancelExecution && !IsWorkCancelled() && !IsWorkCompleted();
}

namespace PCGExContextFinalize
{
	// After a mid-flight cancel, holds the context alive via a strong handle pin and drops it from a
	// game-thread tick once the pin count hits 0 -- so the context's UObject/GC teardown runs on the
	// game thread, never a worker. See FPCGExContext::CancelExecution and FAsyncContextPinTracker.
	class FGameThreadFinalizer : public TSharedFromThis<FGameThreadFinalizer>
	{
		TSharedPtr<FPCGContextHandle> Pin;
		TSharedPtr<PCGExMT::FAsyncContextPinTracker> Tracker;

	public:
		FGameThreadFinalizer(TSharedPtr<FPCGContextHandle> InPin, TSharedPtr<PCGExMT::FAsyncContextPinTracker> InTracker)
			: Pin(MoveTemp(InPin))
			  , Tracker(MoveTemp(InTracker))
		{
		}

		// Queues the next poll on the game thread; never drops the pin synchronously, so it is safe to
		// call inline from CancelExecution (a method on the very context being kept alive).
		static void ScheduleNextPoll(const TSharedPtr<FGameThreadFinalizer>& Self)
		{
			if (UPCGExSubSystem* Subsystem = UPCGExSubSystem::GetSubsystemForCurrentWorld())
			{
				Subsystem->RegisterBeginTickAction([Self]() { Self->PollGameThread(); });
			}
			else
			{
				// No subsystem to tick on (e.g. world teardown). Re-enter via the game-thread task queue
				// instead of dropping the pin here: tasks may still be pinning the context (Num() > 0),
				// and PollGameThread is the single place that drops it -- on the game thread, at Num()==0.
				AsyncTask(ENamedThreads::GameThread, [Self]() { Self->PollGameThread(); });
			}
		}

		// Runs on the subsystem's game-thread tick.
		void PollGameThread()
		{
			if (!Pin)
			{
				return;
			}

			if (!Tracker || Tracker->Num() == 0)
			{
				// All async tasks have released the context: drop the keep-alive here. If it is the
				// last reference, the context is destroyed now -- on the game thread, not on a worker.
				Pin.Reset();
				return;
			}

			// Pins still outstanding -- poll again next frame.
			ScheduleNextPoll(SharedThis(this));
		}
	};
}

bool FPCGExContext::CancelExecution(const FString& InReason)
{
	bool bExpected = false;
	if (bWorkCancelled.compare_exchange_strong(bExpected, true, std::memory_order_acq_rel))
	{
		FSharedContext<FPCGExContext> SharedContext(GetWeakSelfHandle());

		if (!bQuietCancellationError && !InReason.IsEmpty())
		{
			PCGE_LOG_C(Error, GraphAndLog, this, FTEXT(InReason));
		}

		PCGEX_TERMINATE_ASYNC

		// Mid-flight cancel: PCG releases this context on the game thread while async tasks may still
		// be pinning it, so the last task to release would otherwise destroy it off-thread. Keep it
		// pinned and let FGameThreadFinalizer drop the pin from a game-thread tick once all pins are
		// gone. No-op when nothing is in flight.
		if (TSharedPtr<FPCGContextHandle> KeepAlive = GetWeakSelfHandle().Pin())
		{
			PCGExMT::ExecuteOnMainThread(
				[Finalizer = MakeShared<PCGExContextFinalize::FGameThreadFinalizer>(KeepAlive, AsyncPinTracker)]()
				{
					PCGExContextFinalize::FGameThreadFinalizer::ScheduleNextPoll(Finalizer);
				});
		}

		OutputData.Reset();
		if (bPropagateAbortedExecution)
		{
			OutputData.bCancelExecution = true;
		}

		UnpauseContext();
	}

	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
