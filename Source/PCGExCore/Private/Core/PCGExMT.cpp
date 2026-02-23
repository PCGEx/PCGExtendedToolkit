// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExMT.h"

#include "PCGExCoreSettingsCache.h"
#include "Tasks/Task.h"
#include "Async/Async.h"
#include "Core/PCGExContext.h"
#include "PCGExLog.h"
#include "PCGExSettingsCacheBody.h"
#include "Core/PCGExSettings.h"
#include "PCGExSubSystem.h"
#include "Misc/ScopeRWLock.h"
#include "HAL/PlatformTime.h"

#define PCGEX_TASK_LOG(...) //UE_LOG(__VA_ARGS__)
#define PCGEX_MULTI_LOG(...) //UE_LOG(__VA_ARGS__)
#define PCGEX_MANAGER_LOG(...) //UE_LOG(__VA_ARGS__)

namespace PCGExMT
{
	int32 GetSanitizedBatchSize(const int32 NumIterations, const int32 DesiredBatchSize)
	{
		// Clamp chunk sizes to avoid over- or under-subscribing CPU cores.
		// MaxChunkSize (Iterations / Cores*4) caps parallelism to ~4 tasks per core.
		// MinChunkSize (Iterations / Cores*2) ensures at least ~2 tasks per core.
		// Large requested sizes (>128) are promoted to at least MinChunkSize to prevent
		// too few tasks from starving cores, while small sizes are kept as-is for fine-grained work.
		const int32 NumCores = FPlatformMisc::NumberOfCores();
		const int32 MaxChunkSize = FMath::DivideAndRoundUp(NumIterations, NumCores * 4);
		const int32 MinChunkSize = FMath::DivideAndRoundUp(NumIterations, NumCores * 2);

		const int32 BaseChunk = (DesiredBatchSize > 128)
			                        ? FMath::Max(DesiredBatchSize, MinChunkSize)
			                        : FMath::Max(1, DesiredBatchSize);

		return FMath::Clamp(BaseChunk, 1, MaxChunkSize);
	}

	int32 SubLoopScopes(TArray<FScope>& OutSubRanges, const int32 NumIterations, const int32 RangeSize)
	{
		OutSubRanges.Empty();
		OutSubRanges.Reserve((NumIterations + RangeSize - 1) / RangeSize);
		for (int32 Idx = 0; Idx < NumIterations; Idx += RangeSize)
		{
			OutSubRanges.Emplace(Idx, FMath::Min(RangeSize, NumIterations - Idx), OutSubRanges.Num());
		}
		return OutSubRanges.Num();
	}

	// IAsyncHandle
	IAsyncHandle::~IAsyncHandle()
	{
		if (GetState() != EAsyncHandleState::Ended)
		{
			Cancel();
			Complete();
		}
	}

	FTaskManager* IAsyncHandle::GetManager() const
	{
		if (const TSharedPtr<IAsyncHandleGroup> ParentGroup = Group.Pin()) { return ParentGroup->GetManager(); }
		return nullptr;
	}

	bool IAsyncHandle::SetGroup(const TSharedPtr<IAsyncHandleGroup>& InGroup)
	{
		check(!Group.IsValid());
		if (!InGroup) { return false; }

		Group = InGroup;
		PCGEX_TASK_LOG(LogTemp, Warning, TEXT("IAsyncHandle[#%d|%s]::SetParent to [#%d|%s]"), HandleIdx, *DEBUG_HandleId(), InGroup->HandleIdx, *InGroup->DEBUG_HandleId());

		// Only register with the parent group if not already counted as "expected".
		// FRegistrationGuard suppresses completion checks during registration to prevent
		// the group from completing before all siblings are registered.
		if (!bExpected)
		{
			IAsyncHandleGroup::FRegistrationGuard Guard(InGroup);

			bExpected = true;
			InGroup->RegisterExpected();
		}

		return true;
	}

	bool IAsyncHandle::Start()
	{
		if (!TryTransitionState(EAsyncHandleState::Idle, EAsyncHandleState::Running))
		{
			PCGEX_TASK_LOG(LogTemp, Error, TEXT("IAsyncHandle[#%d|%s]::Start (aborted)"), HandleIdx, *DEBUG_HandleId());
			Cancel();
			Complete();
			return false;
		}

		PCGEX_TASK_LOG(LogTemp, Warning, TEXT("IAsyncHandle[#%d|%s]::Start"), HandleIdx, *DEBUG_HandleId());
		if (const TSharedPtr<IAsyncHandleGroup> Parent = Group.Pin()) { Parent->NotifyStarted(); }
		return true;
	}

	void IAsyncHandle::Cancel()
	{
		bool Expected = false;
		if (!bCancelled.compare_exchange_strong(Expected, true, std::memory_order_acq_rel)) { return; }

		// Try to end immediately if idle
		if (TryTransitionState(EAsyncHandleState::Idle, EAsyncHandleState::Ended))
		{
			PCGEX_TASK_LOG(LogTemp, Warning, TEXT("IAsyncHandle[#%d|%s]::Cancel"), HandleIdx, *DEBUG_HandleId());
			OnEnd(true);
		}
	}

	void IAsyncHandle::Complete()
	{
		if (TryTransitionState(EAsyncHandleState::Running, EAsyncHandleState::Ended))
		{
			PCGEX_TASK_LOG(LogTemp, Warning, TEXT("IAsyncHandle[#%d|%s]::Complete"), HandleIdx, *DEBUG_HandleId());
			OnEnd(IsCancelled());
		}
	}

	bool IAsyncHandle::TryTransitionState(EAsyncHandleState From, EAsyncHandleState To)
	{
		return State.compare_exchange_strong(From, To, std::memory_order_acq_rel);
	}

	void IAsyncHandle::OnEnd(const bool bWasCancelled)
	{
		PCGEX_TASK_LOG(LogTemp, Warning, TEXT("IAsyncHandle[#%d|%s]::OnEnd"), HandleIdx, *DEBUG_HandleId());
		if (const TSharedPtr<IAsyncHandleGroup> Parent = Group.Pin())
		{
			Parent->NotifyCompleted();
		}
	}

	FSchedulingScope::FSchedulingScope(const TSharedPtr<FTaskManager>& InManager)
	{
		Token = InManager ? InManager->TryCreateToken(FName("SchedulingScope")) : nullptr;
	}

	FSchedulingScope::~FSchedulingScope()
	{
		if (const TSharedPtr<FAsyncToken> PinnedToken = Token.Pin())
		{
			PinnedToken->Release();
			Token.Reset();
		}
	}

	// IAsyncMultiHandle
	IAsyncHandleGroup::IAsyncHandleGroup(const FName InName)
		: GroupName(InName)
	{
	}

	IAsyncHandleGroup::~IAsyncHandleGroup()
	{
	}

	bool IAsyncHandleGroup::RegisterExpected(int32 Count)
	{
		if (!IsAvailable()) { return false; }
		ExpectedCount.fetch_add(Count, std::memory_order_acq_rel);
		PCGEX_MULTI_LOG(LogTemp, Warning, TEXT("IAsyncMultiHandle[#%d|%s]::RegisterExpected +%d (%d)"), HandleIdx, *DEBUG_HandleId(), Count, ExpectedCount.load());
		return true;
	}

	void IAsyncHandleGroup::NotifyStarted()
	{
		//if (!IsAvailable()) { return; }
		StartedCount.fetch_add(1, std::memory_order_acq_rel);
		PCGEX_MULTI_LOG(LogTemp, Warning, TEXT("IAsyncMultiHandle[#%d|%s]::NotifyStarted++ (%d)"), HandleIdx, *DEBUG_HandleId(), StartedCount.load());
	}

	void IAsyncHandleGroup::NotifyCompleted()
	{
		//if (!IsAvailable()) { return; }

		CompletedCount.fetch_add(1, std::memory_order_acq_rel);
		PCGEX_MULTI_LOG(LogTemp, Warning, TEXT("IAsyncMultiHandle[#%d|%s]::NotifyCompleted++ (%d)"), HandleIdx, *DEBUG_HandleId(), CompletedCount.load());

		CheckCompletion();
	}

	void IAsyncHandleGroup::Launch(const TSharedPtr<FTask>& InTask, const bool bIsExpected)
	{
		InTask->bExpected = bIsExpected;
		LaunchInternal(InTask);
	}

	int32 IAsyncHandleGroup::Launch(const int32 Count, FCreateLaunchablePredicate&& Predicate)
	{
		if (!CanScheduleWork()) { return 0; }

		TArray<TSharedPtr<FTask>> Handles;
		Handles.Reserve(Count);

		for (int i = 0; i < Count; i++) { if (TSharedPtr<FTask> Task = Predicate(i)) { Handles.Add(Task); } }
		StartHandlesBatchImpl(Handles);

		return Handles.Num();
	}

	TWeakPtr<FAsyncToken> IAsyncHandleGroup::TryCreateToken(const FName& InName)
	{
		if (!CanScheduleWork()) { return nullptr; }

		FWriteScopeLock WriteLock(TokenLock);
		PCGEX_MAKE_SHARED(Token, FAsyncToken, SharedThis(this))
		return Tokens.Add_GetRef(Token);
	}

	int32 IAsyncHandleGroup::RegisterTask(const TSharedPtr<IAsyncHandle>& InTask)
	{
		FWriteScopeLock WriteLock(RegistryLock);
		return Registry.Add(InTask);
	}

	void IAsyncHandleGroup::ClearRegistry(const bool bCancel)
	{
		TArray<TSharedPtr<FAsyncToken>> TempTokens;
		{
			FWriteScopeLock WriteLock(TokenLock);
			TempTokens = MoveTemp(Tokens);
			Tokens.Empty();
		}

		if (bCancel)
		{
			TArray<TSharedPtr<IAsyncHandle>> HandlesToCancel;

			{
				FWriteScopeLock WriteLock(RegistryLock);
				HandlesToCancel.Reserve(Registry.Num());
				for (const TWeakPtr<IAsyncHandle>& Weak : Registry)
				{
					if (TSharedPtr<IAsyncHandle> Handle = Weak.Pin()) { HandlesToCancel.Add(Handle); }
				}
				Registry.Empty();
			}

			// Cancel outside locks
			for (const TSharedPtr<IAsyncHandle>& Handle : HandlesToCancel) { Handle->Cancel(); }
		}
		else
		{
			{
				FWriteScopeLock WriteLock(RegistryLock);
				Registry.Empty();
			}
		}
	}

	void IAsyncHandleGroup::CheckCompletion()
	{
		EAsyncHandleState CurrentState = GetState();
		if (CurrentState == EAsyncHandleState::Ended) { return; }

		// FRegistrationGuard increments PendingRegistrations to suppress premature completion.
		// Without this, a fast task could complete and trigger group completion before
		// all sibling tasks are registered by the batch launcher.
		if (PendingRegistrations.load(std::memory_order_acquire) > 0) { return; }

		// Full fence ensures visibility of all state set during registration/completion
		// across all threads before we read the counters below.
		std::atomic_thread_fence(std::memory_order_seq_cst);

		const int32 Expected = ExpectedCount.load(std::memory_order_acquire);
		const int32 Started = StartedCount.load(std::memory_order_acquire);
		const int32 Completed = CompletedCount.load(std::memory_order_acquire);

		// All three counters must agree: every expected task has both started AND completed.
		// The Started == Completed check prevents completing while tasks are still running.
		if (Completed >= Expected && Completed == Started && Expected > 0)
		{
			// CAS ensures exactly one thread transitions to Ended and fires OnEnd.
			EAsyncHandleState RunningState = EAsyncHandleState::Running;
			if (State.compare_exchange_strong(RunningState, EAsyncHandleState::Ended, std::memory_order_acq_rel))
			{
				OnEnd(IsCancelled());
			}
		}
	}

	void IAsyncHandleGroup::StartHandlesBatchImpl(const TArray<TSharedPtr<FTask>>& InHandles)
	{
		if (!CanScheduleWork()) { return; }

		if (InHandles.IsEmpty())
		{
			AssertEmptyThread();
			return;
		}

		PCGEX_SHARED_THIS_DECL
		FTaskManager* Manager = GetManager();
		if (!Manager) { return; }

		{
			FWriteScopeLock WriteLock(RegistryLock);
			FRegistrationGuard Guard(ThisPtr);

			RegisterExpected(InHandles.Num());
			Registry.Reserve(Registry.Num() + InHandles.Num());

			for (const TSharedPtr<FTask>& Task : InHandles)
			{
				Task->HandleIdx = Registry.Add(Task);
				Task->bExpected = true;
				Task->SetGroup(ThisPtr);
			}
		}

		for (const TSharedPtr<FTask>& Task : InHandles) { Manager->LaunchInternal(Task); }
	}

	void IAsyncHandleGroup::AssertEmptyThread() const
	{
		FTaskManager* Manager = GetManager();
		UE_LOG(LogPCGEx, Error, TEXT( "[%s @ %s] Empty thread - Graph will hang until cancelled. Enable bAssertOnEmptyThread for stack trace; Please head out to PCGEx Discord or log an issue on git." ), Manager ? *GetNameSafe(Manager->GetContext()->GetInputSettings<UPCGExSettings>()) : TEXT( "UNKNOWN NODE"), *DEBUG_HandleId());
		if (PCGEX_CORE_SETTINGS.bAssertOnEmptyThread) { ensure(false); }
	}

	bool IAsyncHandleGroup::IsAvailable() const
	{
		if (IsCancelled() || GetState() == EAsyncHandleState::Ended) { return false; }
		if (const FTaskManager* Manager = GetManager()) { return Manager->IsAvailable(); }
		return false;
	}

	void IAsyncHandleGroup::Cancel()
	{
		bool Expected = false;
		if (!bCancelled.compare_exchange_strong(Expected, true, std::memory_order_acq_rel)) { return; }

		ClearRegistry(true);

		// Try to end immediately if idle
		if (TryTransitionState(EAsyncHandleState::Idle, EAsyncHandleState::Ended))
		{
			PCGEX_TASK_LOG(LogTemp, Warning, TEXT("IAsyncHandle[#%d|%s]::Cancel"), HandleIdx, *DEBUG_HandleId());
			OnEnd(true);
		}
	}

	bool IAsyncHandleGroup::CanScheduleWork() { return IsAvailable(); }

	void IAsyncHandleGroup::LaunchInternal(const TSharedPtr<FTask>& InTask)
	{
		if (!IsAvailable()) { return; }

		PCGEX_MULTI_LOG(LogTemp, Warning, TEXT("IAsyncMultiHandle[#%d|%s]::LaunchTask"), HandleIdx, *DEBUG_HandleId()) if (FTaskManager* Manager = GetManager())
		{
			// Register in this handle's registry
			const int32 Idx = RegisterTask(InTask);
			InTask->HandleIdx = Idx;
			InTask->SetGroup(SharedThis(this));

			// Launch through manager
			Manager->LaunchInternal(InTask);
		}
	}

	void IAsyncHandleGroup::OnEnd(const bool bWasCancelled)
	{
		const FTaskManager* Manager = GetManager();
		PCGEX_MULTI_LOG(LogTemp, Warning, TEXT("IAsyncMultiHandle[[%s]#%d|%s]::OnEnd(%d)"), Manager ? *GetNameSafe(Manager->GetContext()->GetInputSettings<UPCGExSettings>()) : TEXT(""), HandleIdx, *DEBUG_HandleId(), bWasCancelled)

		// Clear registry to free memory
		ClearRegistry();

		if (!bWasCancelled && OnCompleteCallback)
		{
			FCompletionCallback LocalCallback = MoveTemp(OnCompleteCallback);
			LocalCallback();
		}

		if (const TSharedPtr<IAsyncHandleGroup> Parent = Group.Pin())
		{
			Parent->NotifyCompleted();
		}
	}

	// FAsyncToken acts as a RAII-style reference count on a group.
	// Creating a token registers +1 expected/started task on the parent group;
	// releasing (or destroying) the token marks that slot as completed.
	// This keeps the group alive while non-task work (e.g. scheduling, async lambdas) is in flight.
	FAsyncToken::FAsyncToken(const TWeakPtr<IAsyncHandleGroup>& InHandle)
		: Group(InHandle)
	{
		if (const TSharedPtr<IAsyncHandleGroup> Pinned = Group.Pin())
		{
			PCGEX_TASK_LOG(LogTemp, Error, TEXT("FAsyncToken::FAsyncToken @#%d|%s"), Pinned->HandleIdx, *Pinned->DEBUG_HandleId());
			Pinned->RegisterExpected();
			Pinned->NotifyStarted();
		}
	}

	FAsyncToken::~FAsyncToken() { Release(); }

	void FAsyncToken::Release()
	{
		bool Expected = false;
		if (bReleased.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
		{
			if (const TSharedPtr<IAsyncHandleGroup> Pinned = Group.Pin())
			{
				Pinned->NotifyCompleted();
				Group.Reset();
			}
		}
	}

	// FTaskManager
	FTaskManager::FTaskManager(FPCGExContext* InContext)
		: IAsyncHandleGroup(FName("MANAGER")), Context(InContext), ContextHandle(InContext->GetOrCreateHandle())
	{
		WorkHandle = Context->GetWorkHandle();
	}

	FTaskManager::~FTaskManager()
	{
	}

	FTaskManager* FTaskManager::GetManager() const
	{
		return const_cast<FTaskManager*>(this);
	}

	bool FTaskManager::IsAvailable() const
	{
		return ContextHandle.IsValid() && WorkHandle.IsValid() && !IsCancelled();
	}

	bool FTaskManager::IsWaitingForTasks() const
	{
		return GetState() == EAsyncHandleState::Running;
	}

	bool FTaskManager::Start()
	{
		if (IsCancelled()) { return false; }

		// Pause the PCG context so the PCG scheduler doesn't tick ExecuteInternal
		// while async work is in flight. Context is unpaused when all work completes.
		Context->PauseContext();

		// Auto-reset from Ended state allows the same manager to be reused across
		// multiple rounds of async work within a single node execution.
		EAsyncHandleState CurrentState = GetState();
		if (CurrentState == EAsyncHandleState::Ended)
		{
			Reset();
			CurrentState = EAsyncHandleState::Idle;
		}

		if (CurrentState == EAsyncHandleState::Idle)
		{
			if (TryTransitionState(EAsyncHandleState::Idle, EAsyncHandleState::Running))
			{
				PCGEX_MANAGER_LOG(LogTemp, Warning, TEXT("FTaskManager::Start"));
				return true;
			}
		}

		return GetState() == EAsyncHandleState::Running;
	}

	void FTaskManager::Cancel()
	{
		// Don't call regular flow on cancellation, this is scorched eath.
		// Execution has been cancelled, we just need to cancel all ongoing tasks

		bool Expected = false;
		if (!bCancelled.compare_exchange_strong(Expected, true, std::memory_order_acq_rel)) { return; }

		ClearRegistry(true);
	}

	void FTaskManager::ClearGroups()
	{
		FWriteScopeLock WriteLock(GroupsLock);
		Groups.Empty();
	}

	void FTaskManager::Reset()
	{
		if (IsCancelled())
		{
			PCGEX_MANAGER_LOG(LogTemp, Error, TEXT("FTaskManager::Reset but manager was cancelled"));
			return;
		}

		bool Expected = false;
		if (bResetting.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
		{
			PCGEX_MANAGER_LOG(LogTemp, Warning, TEXT("FTaskManager::Reset"));

			TArray<TSharedPtr<FAsyncToken>> TempTokens;
			{
				FWriteScopeLock WriteLock(TokenLock);
				TempTokens = MoveTemp(Tokens);
				Tokens.Empty();
			}

			{
				FWriteScopeLock WriteLock(RegistryLock);
				Registry.Empty();
			}

			{
				FWriteScopeLock WriteLock(GroupsLock);
				Groups.Empty();
			}

			// Reset counters
			ExpectedCount.store(0, std::memory_order_release);
			StartedCount.store(0, std::memory_order_release);
			CompletedCount.store(0, std::memory_order_release);
			bCancelled.store(false, std::memory_order_release);

			State.store(EAsyncHandleState::Idle, std::memory_order_release);

			bResetting.store(false, std::memory_order_release);
		}
	}

	bool FTaskManager::CanScheduleWork()
	{
		return Start();
	}

	TSharedPtr<FTaskGroup> FTaskManager::TryCreateTaskGroup(const FName& InName, const TSharedPtr<IAsyncHandleGroup>& InParentHandle)
	{
		if (!CanScheduleWork()) { return nullptr; }

		PCGEX_MANAGER_LOG(LogTemp, Warning, TEXT("FTaskManager::TryCreateTaskGroup (%s)"), *InName.ToString());

		PCGEX_MAKE_SHARED(NewGroup, FTaskGroup, InName)

		int32 Idx = -1;
		{
			FWriteScopeLock WriteLock(GroupsLock);
			Idx = Groups.Add(NewGroup) + 1;
		}

		NewGroup->HandleIdx = Idx * -1;

		PCGEX_SHARED_THIS_DECL
		if (NewGroup->SetGroup(InParentHandle ? InParentHandle : ThisPtr))
		{
			NewGroup->Start();
			return NewGroup;
		}

		return nullptr;
	}

	bool FTaskManager::TryRegisterHandle(const TSharedPtr<IAsyncHandle>& InHandle, const TSharedPtr<IAsyncHandleGroup>& InParentHandle)
	{
		if (!CanScheduleWork()) { return false; }

		const int32 Idx = RegisterTask(InHandle);
		InHandle->HandleIdx = Idx;

		PCGEX_SHARED_THIS_DECL
		if (InHandle->SetGroup(InParentHandle ? InParentHandle : ThisPtr))
		{
			InHandle->Start();
			return true;
		}

		return false;
	}

	void FTaskManager::LaunchInternal(const TSharedPtr<FTask>& InTask)
	{
		if (!CanScheduleWork()) { return; }

		PCGEX_MANAGER_LOG(LogTemp, Warning, TEXT("FTaskManager::LaunchTask : [%d|%s]"), InTask->HandleIdx, *InTask->DEBUG_HandleId());
		PCGEX_SHARED_THIS_DECL

		// Orphan tasks (no explicit group) are parented directly to the manager.
		if (!InTask->Group.IsValid())
		{
			InTask->HandleIdx = RegisterTask(InTask);
			InTask->SetGroup(ThisPtr);
		}

		UE::Tasks::Launch(*InTask->DEBUG_HandleId(), [WeakManager = TWeakPtr<FTaskManager>(SharedThis(this)), Task = InTask]()
		{
#define PCGEX_CANCEL_TASK_INTERNAL Task->Cancel(); Task->Complete(); return;

			const TSharedPtr<FTaskManager> Manager = WeakManager.Pin();
			if (!Manager || !Manager->IsAvailable()) { PCGEX_CANCEL_TASK_INTERNAL }

			{
				// FSharedContext pins the PCG context via its handle, preventing it from
				// being destroyed while this task runs. Without this, the context could be
				// garbage-collected mid-execution if the PCG graph is torn down.
				FPCGContext::FSharedContext<FPCGExContext> SharedContext(Manager->ContextHandle);
				if (!SharedContext.Get()) { PCGEX_CANCEL_TASK_INTERNAL }

#undef PCGEX_CANCEL_TASK_INTERNAL

				if (Task->Start())
				{
					Task->ExecuteTask(Manager);
					Task->Complete();
				}
			}
		});
	}

	void FTaskManager::OnEnd(const bool bWasCancelled)
	{
		const FTaskManager* Manager = this;
		PCGEX_MULTI_LOG(LogTemp, Warning, TEXT("FTaskManager[[%s]#%d|%s]::OnEnd(%d)"), Manager ? *GetNameSafe(Manager->GetContext()->GetInputSettings<UPCGExSettings>()) : TEXT(""), HandleIdx, *DEBUG_HandleId(), bWasCancelled)

		// Clear registries
		ClearRegistry();

		// For the manager, we DON'T call parent notification (there is no parent)
		// We call OnEndCallback directly, which notifies the context

		// Run the completion callback first (for consistency with IAsyncMultiHandle)
		if (!bWasCancelled && OnCompleteCallback)
		{
			const FCompletionCallback Callback = MoveTemp(OnCompleteCallback);
			Callback();
		}

		// Then the end callback (context notification)
		// DriveAdvanceWork handles coordination if called while already driving
		if (OnEndCallback)
		{
			OnEndCallback(bWasCancelled);
		}
	}

	void FTaskManager::ClearRegistry(const bool bCancel)
	{
		bool Expected = false;

		if (bCancel)
		{
			TArray<TSharedPtr<IAsyncHandle>> HandlesToCancel;

			TArray<TSharedPtr<FAsyncToken>> TempTokens;
			{
				FWriteScopeLock WriteLock(TokenLock);
				TempTokens = MoveTemp(Tokens);
				Tokens.Empty();
			}

			{
				FWriteScopeLock WriteLock(RegistryLock);

				HandlesToCancel.Reserve(Registry.Num() + Groups.Num());
				for (const TWeakPtr<IAsyncHandle>& Weak : Registry) { if (TSharedPtr<IAsyncHandle> Handle = Weak.Pin()) { HandlesToCancel.Add(Handle); } }

				Registry.Empty();
			}

			{
				FWriteScopeLock WriteLock(GroupsLock);
				for (const TSharedPtr<FTaskGroup>& LocalGroup : Groups) { HandlesToCancel.Add(LocalGroup); }
				Groups.Empty();
			}

			// Cancel outside locks
			for (const TSharedPtr<IAsyncHandle>& Handle : HandlesToCancel) { Handle->Cancel(); }
		}
		else
		{
			IAsyncHandleGroup::ClearRegistry(false);

			{
				FWriteScopeLock WriteLock(GroupsLock);
				Groups.Empty();
			}
		}
	}

	// FTaskGroup
	FTaskGroup::FTaskGroup(const FName InName)
		: IAsyncHandleGroup(InName)
	{
	}

	void FTaskGroup::StartIterations(const int32 NumIterations, const int32 ChunkSize, const bool bForceSingleThreaded, const bool bPreparationOnly)
	{
		if (!IsAvailable() || (!bPreparationOnly && !OnIterationCallback)) { return; }

		if (!NumIterations)
		{
			AssertEmptyThread();
			return;
		}

		const int32 SanitizedChunk = GetSanitizedBatchSize(NumIterations, ChunkSize);

		if (bForceSingleThreaded)
		{
			TArray<FScope> Loops;
			const int32 NumScopes = SubLoopScopes(Loops, NumIterations, SanitizedChunk);

			{
				FRegistrationGuard Guard(SharedThis(this));

				RegisterExpected(NumScopes);
				if (OnPrepareSubLoopsCallback) { OnPrepareSubLoopsCallback(Loops); }

				PCGEX_MAKE_SHARED(Task, FScopeIterationTask)
				Task->bPrepareOnly = bPreparationOnly;
				Task->Scope = Loops[0];
				Task->NumIterations = NumIterations;
				Launch(Task, true);
			}
		}
		else
		{
			StartRanges<FScopeIterationTask>(NumIterations, SanitizedChunk, bPreparationOnly);
		}
	}

	void FTaskGroup::StartSubLoops(const int32 NumIterations, const int32 ChunkSize, const bool bForceSingleThreaded)
	{
		StartIterations(NumIterations, ChunkSize, bForceSingleThreaded, true);
	}

	void FTaskGroup::AddSimpleCallback(FSimpleCallback&& InCallback)
	{
		SimpleCallbacks.Add(InCallback);
	}

	void FTaskGroup::StartSimpleCallbacks()
	{
		const int32 Count = SimpleCallbacks.Num();
		TArray<TSharedPtr<FTask>> Tasks;
		Tasks.Reserve(Count);

		for (int i = 0; i < Count; i++)
		{
			PCGEX_MAKE_SHARED(Task, FSimpleCallbackTask, i)
			Tasks.Add(Task);
		}

		StartHandlesBatchImpl(Tasks);
	}

	void FTaskGroup::ExecScopeIteration(const FScope& Scope, const bool bPrepareOnly) const
	{
		if (!IsAvailable()) { return; }
		if (OnSubLoopStartCallback) { OnSubLoopStartCallback(Scope); }
		if (!bPrepareOnly) { PCGEX_SCOPE_LOOP(i) { OnIterationCallback(i, Scope); } }
	}

	void FTaskGroup::TriggerSimpleCallback(int32 Index)
	{
		if (!IsAvailable() || !SimpleCallbacks.IsValidIndex(Index)) { return; }
		SimpleCallbacks[Index]();
	}

	void ExecuteOnMainThread(const TSharedPtr<IAsyncHandleGroup>& ParentHandle, FExecuteCallback&& Callback)
	{
		if (IsInGameThread())
		{
			Callback();
			return;
		}

		TWeakPtr<FAsyncToken> TokenWeakPtr = ParentHandle->TryCreateToken(FName("ExecuteOnMainThread"));
		AsyncTask(ENamedThreads::GameThread, [TokenWeakPtr, Callback]()
		{
			if (!TokenWeakPtr.IsValid()) { return; }
			Callback();
			PCGEX_ASYNC_RELEASE_CAPTURED_TOKEN(TokenWeakPtr)
		});
	}

	void ExecuteOnMainThread(FExecuteCallback&& Callback)
	{
		if (IsInGameThread())
		{
			Callback();
			return;
		}
		
		AsyncTask(ENamedThreads::GameThread, Callback);
	}

	void ExecuteOnMainThreadAndWait(FExecuteCallback&& Callback)
	{
		// We're not in the game thread, we need to dispatch loading to the main thread
		// and wait in the current one
		const FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[Callback]() { Callback(); },
			TStatId(), nullptr, ENamedThreads::GameThread);

		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
	}

	void FTask::Launch(const TSharedPtr<FTask>& InTask, const bool bIsExpected) const
	{
		if (const TSharedPtr<IAsyncHandleGroup> Parent = Group.Pin()) { Parent->Launch(InTask, bIsExpected); }
		else if (FTaskManager* Manager = GetManager()) { Manager->Launch(InTask, bIsExpected); }
	}

	// Task implementations
	void FSimpleCallbackTask::ExecuteTask(const TSharedPtr<FTaskManager>& TaskManager)
	{
		if (const TSharedPtr<IAsyncHandleGroup> Parent = Group.Pin())
		{
			StaticCastSharedPtr<FTaskGroup>(Parent)->TriggerSimpleCallback(TaskIndex);
		}
	}

	void FScopeIterationTask::ExecuteTask(const TSharedPtr<FTaskManager>& TaskManager)
	{
		const TSharedPtr<IAsyncHandleGroup> Parent = Group.Pin();
		if (!Parent) { return; }

		const TSharedPtr<FTaskGroup> TaskGroup = StaticCastSharedPtr<FTaskGroup>(Parent);
		if (!TaskGroup->IsAvailable()) { return; }

		TaskGroup->ExecScopeIteration(Scope, bPrepareOnly);

		// When NumIterations != -1, this task chains into the next scope sequentially.
		// This is used for forced single-threaded execution: instead of launching all
		// scopes in parallel, each scope task spawns the next one after completing,
		// ensuring strictly sequential iteration order.
		if (NumIterations != -1)
		{
			FScope NextScope = FScope(Scope.End, FMath::Min(NumIterations - Scope.End, Scope.Count), Scope.LoopIndex + 1);
			if (NextScope.IsValid())
			{
				PCGEX_MAKE_SHARED(Task, FScopeIterationTask)
				Task->bPrepareOnly = bPrepareOnly;
				Task->Scope = NextScope;
				Task->NumIterations = NumIterations;
				Parent->Launch(Task, true);
			}
		}
	}

	// IExecuteOnMainThread provides time-sliced execution on the game thread.
	// Work is broken into frames via the subsystem's begin-tick action queue.
	// Each frame, Execute() runs until ShouldStop() (time budget exceeded) returns true,
	// then re-schedules itself for the next frame. This prevents blocking the game thread
	// while still making progress on work that requires game-thread access.
	IExecuteOnMainThread::IExecuteOnMainThread()
	{
	}

	bool IExecuteOnMainThread::Start()
	{
		if (!IAsyncHandle::Start()) { return false; }
		Schedule();
		return true;
	}

	void IExecuteOnMainThread::Schedule()
	{
		if (IsCancelled() || GetState() != EAsyncHandleState::Running)
		{
			Complete();
			return;
		}

		// Register a callback on the subsystem's next tick. The subsystem provides
		// an EndTime deadline so we know how much time budget remains this frame.
		PCGEX_SUBSYSTEM
		PCGExSubsystem->RegisterBeginTickAction([PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			PCGEX_SUBSYSTEM
			This->EndTime = PCGExSubsystem->GetEndTime();
			if (!This->Execute()) { This->Schedule(); }
			else { This->Complete(); }
		});
	}

	bool IExecuteOnMainThread::Execute()
	{
		return true; // Override in derived
	}

	void IExecuteOnMainThread::OnEnd(bool bWasCancelled)
	{
		if (!bWasCancelled && OnCompleteCallback)
		{
			const FCompletionCallback Callback = MoveTemp(OnCompleteCallback);
			Callback();
		}
		IAsyncHandle::OnEnd(bWasCancelled);
	}

	bool IExecuteOnMainThread::ShouldStop()
	{
		return FPlatformTime::Seconds() > EndTime;
	}

	FTimeSlicedMainThreadLoop::FTimeSlicedMainThreadLoop(const int32 NumIterations)
		: Scope(FScope(0, NumIterations, 0))
	{
	}

	bool FTimeSlicedMainThreadLoop::Start()
	{
		check(OnIterationCallback)
		return IExecuteOnMainThread::Start();
	}

	void FTimeSlicedMainThreadLoop::Cancel()
	{
		IExecuteOnMainThread::Cancel();
		Complete();
	}

	bool FTimeSlicedMainThreadLoop::Execute()
	{
		if (IsCancelled() || Scope.Start >= Scope.End) { return true; }

		FPCGExContext* InContext = nullptr;
		if (FTaskManager* Manager = GetManager())
		{
			if (Manager->IsAvailable()) { InContext = Manager->GetContext(); }
		}

		if (!InContext) { return true; }

		// Process iterations until the frame's time budget is exhausted.
		// On budget expiry, advance Scope.Start so the next frame resumes
		// where we left off rather than re-processing completed iterations.
		PCGEX_SCOPE_LOOP(Index)
		{
			OnIterationCallback(Index, Scope);
			if (ShouldStop())
			{
				Scope.Start = Index + 1;
				Scope.LoopIndex++;
				return false;
			}
		}

		return true;
	}
}

#undef PCGEX_TASK_LOG
