// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <atomic>
#include <functional>

#include "CoreMinimal.h"
#include "PCGExMTCommon.h"
#include "Async/AsyncWork.h"
#include "Misc/QueuedThreadPool.h"
#include "Tasks/Task.h"
#include "Templates/SharedPointer.h"
#include "Templates/SharedPointerFwd.h"
#include "UObject/ObjectPtr.h"

#include "PCGExCoreMacros.h"

namespace PCGEx
{
	class FWorkHandle;
}

struct FPCGContextHandle;
enum class EPCGExAsyncPriority : uint8;
struct FPCGExContext;

namespace PCGExMT
{
	PCGEXCORE_API
	int32 GetSanitizedBatchSize(const int32 NumIterations, const int32 DesiredBatchSize);

	PCGEXCORE_API
	int32 SubLoopScopes(TArray<FScope>& OutSubRanges, const int32 NumIterations, const int32 RangeSize);

	enum class EAsyncHandleState : uint8
	{
		Idle    = 0,
		Running = 1,
		Ended   = 2,
	};

	class IAsyncHandleGroup;
	class FAsyncToken;
	class FTask;
	class FTaskGroup;
	class FTaskManager;

	// Counts async tasks currently pinning the owning context. Lives in its own shared object (not on
	// the context) so the cancel finalizer can poll it WITHOUT pinning the context, and thus learn
	// when every worker has let go -- so the final delete can run on the game thread, not off-thread.
	class PCGEXCORE_API FAsyncContextPinTracker : public TSharedFromThis<FAsyncContextPinTracker>
	{
		std::atomic<int32> PinCount{0};

	public:
		void Increment() { PinCount.fetch_add(1, std::memory_order_acq_rel); }
		void Decrement() { PinCount.fetch_sub(1, std::memory_order_acq_rel); }
		int32 Num() const { return PinCount.load(std::memory_order_acquire); }
	};

	// RAII bracket for the context-pinned region of an async task body. MUST be the task body's FIRST
	// action -- before any cancellation gate and before the FSharedContext pin (so it destructs after
	// that pin releases). That ordering lets a zero tracker count mean "no task holds or can still
	// acquire a pin": a task that may pin is already counted, and one starting after a cancel
	// increments then bails at its gate without pinning. Every independently-scheduled off-thread task
	// that pins MUST use this; sub-tasks pinning under a synchronous join are covered by the parent.
	struct FAsyncContextPinScope
	{
		TSharedPtr<FAsyncContextPinTracker> Tracker;

		explicit FAsyncContextPinScope(const TSharedPtr<FAsyncContextPinTracker>& InTracker)
			: Tracker(InTracker)
		{
			if (Tracker) { Tracker->Increment(); }
		}

		~FAsyncContextPinScope()
		{
			if (Tracker) { Tracker->Decrement(); }
		}

		FAsyncContextPinScope(const FAsyncContextPinScope&) = delete;
		FAsyncContextPinScope& operator=(const FAsyncContextPinScope&) = delete;
	};

	// Base async handle with state management
	class PCGEXCORE_API IAsyncHandle : public TSharedFromThis<IAsyncHandle>
	{
		friend class IAsyncHandleGroup;
		friend class FAsyncToken;
		friend class FTask;
		friend class FTaskGroup;
		friend class FTaskManager;

	protected:
		int8 bExpected = false;
		TWeakPtr<IAsyncHandleGroup> Group;

		std::atomic<bool> bResetting{false};
		std::atomic<bool> bCancelled{false};
		std::atomic<EAsyncHandleState> State{EAsyncHandleState::Idle};

	public:
		int32 HandleIdx = -1;

		virtual FString DEBUG_HandleId() const
		{
			return TEXT("NOT IMPLEMENTED");
		}

		IAsyncHandle() = default;
		virtual ~IAsyncHandle();

		bool IsCancelled() const
		{
			return bCancelled.load(std::memory_order_acquire);
		}

		EAsyncHandleState GetState() const
		{
			return State.load(std::memory_order_acquire);
		}

		virtual FTaskManager* GetManager() const;
		bool SetGroup(const TSharedPtr<IAsyncHandleGroup>& InGroup);

		virtual bool Start();
		virtual void Cancel();
		virtual void Complete();

	protected:
		bool TryTransitionState(EAsyncHandleState From, EAsyncHandleState To);
		virtual void OnEnd(bool bWasCancelled);
	};

#define PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(_MANAGER) PCGExMT::FSchedulingScope SchedulingScope(_MANAGER); if(!SchedulingScope.Token.IsValid())
#define PCGEX_ASYNC_SCHEDULING_SCOPE(_MANAGER, ...) PCGEX_ASYNC_SCHEDULING_SCOPE_BODY(_MANAGER){ return __VA_ARGS__; }

	struct PCGEXCORE_API FSchedulingScope
	{
		TWeakPtr<FAsyncToken> Token;
		explicit FSchedulingScope(const TSharedPtr<FTaskManager>& InManager);
		~FSchedulingScope();
	};

	// Multi-handle manages multiple child tasks
	class PCGEXCORE_API IAsyncHandleGroup : public IAsyncHandle
	{
		friend class FTaskManager;

	protected:
		FName GroupName = NAME_None;

		// Per-handle registry for memory management

		mutable FRWLock RegistryLock;
		TArray<TWeakPtr<IAsyncHandle>> Registry;

		mutable FRWLock TokenLock;
		TArray<TSharedPtr<FAsyncToken>> Tokens;

		std::atomic<int32> PendingRegistrations{0};
		std::atomic<int32> ExpectedCount{0};
		std::atomic<int32> StartedCount{0};
		std::atomic<int32> CompletedCount{0};

	public:
		using FCreateLaunchablePredicate = std::function<TSharedPtr<FTask>(int32)>;

		// RAII guard that blocks CheckCompletion during registration
		struct FRegistrationGuard
		{
			TSharedPtr<IAsyncHandleGroup> Parent;

			explicit FRegistrationGuard(const TSharedPtr<IAsyncHandleGroup>& InParent)
				: Parent(InParent)
			{
				Parent->PendingRegistrations.fetch_add(1, std::memory_order_acquire);
			}

			~FRegistrationGuard()
			{
				const int32 Remaining = Parent->PendingRegistrations.fetch_sub(1, std::memory_order_release) - 1;
				if (Remaining == 0)
				{
					Parent->CheckCompletion();
				}
			}
		};

		virtual FString DEBUG_HandleId() const override
		{
			return GroupName.ToString();
		}

		FCompletionCallback OnCompleteCallback;

		explicit IAsyncHandleGroup(const FName InName);
		virtual ~IAsyncHandleGroup() override;

		virtual bool IsAvailable() const;

		bool RegisterExpected(int32 Count = 1);
		void NotifyStarted();
		void NotifyCompleted();

		void Launch(const TSharedPtr<FTask>& InTask, const bool bIsExpected = false);
		int32 Launch(const int32 Count, FCreateLaunchablePredicate&& Predicate);

		TWeakPtr<FAsyncToken> TryCreateToken(const FName& InName);

		virtual void Cancel() override;

	protected:
		virtual bool CanScheduleWork();
		virtual void LaunchInternal(const TSharedPtr<FTask>& InTask);
		virtual void OnEnd(bool bWasCancelled) override;
		void CheckCompletion();

		int32 RegisterTask(const TSharedPtr<IAsyncHandle>& InTask);
		virtual void ClearRegistry(const bool bCancel = false);

		void StartHandlesBatchImpl(const TArray<TSharedPtr<FTask>>& InHandles);

		void AssertEmptyThread() const;
	};

	// Token for async work tracking
	class PCGEXCORE_API FAsyncToken final : public TSharedFromThis<FAsyncToken>
	{
		std::atomic<bool> bReleased{false};
		TWeakPtr<IAsyncHandleGroup> Group;

	public:
		FAsyncToken(const TWeakPtr<IAsyncHandleGroup>& InHandle);
		~FAsyncToken();
		void Release();
	};

	// Task manager - root of task hierarchy
	class PCGEXCORE_API FTaskManager : public IAsyncHandleGroup
	{
		friend class IAsyncHandle;
		friend class IAsyncHandleGroup;
		friend class FTask;
		friend class FTaskGroup;

	protected:
		TWeakPtr<PCGEx::FWorkHandle> WorkHandle;
		FPCGExContext* Context = nullptr;
		TWeakPtr<FPCGContextHandle> ContextHandle;

		// Groups and tokens managed separately (they start immediately)
		mutable FRWLock GroupsLock;
		TArray<TSharedPtr<FTaskGroup>> Groups;

	public:
		FEndCallback OnEndCallback;
		UE::Tasks::ETaskPriority WorkPriority = UE::Tasks::ETaskPriority::Default;

		explicit FTaskManager(FPCGExContext* InContext);
		virtual ~FTaskManager() override;

		virtual FTaskManager* GetManager() const override;
		virtual bool IsAvailable() const override;
		bool IsWaitingForTasks() const;

		template <typename T>
		T* GetContext() const
		{
			return static_cast<T*>(Context);
		}

		FPCGExContext* GetContext() const
		{
			return Context;
		}

		virtual bool Start() override;
		virtual void Cancel() override;

		TSharedPtr<FTaskGroup> TryCreateTaskGroup(const FName& InName, const TSharedPtr<IAsyncHandleGroup>& InParentHandle = nullptr);
		bool TryRegisterHandle(const TSharedPtr<IAsyncHandle>& InHandle, const TSharedPtr<IAsyncHandleGroup>& InParentHandle = nullptr);

		void Reset();

	protected:
		virtual bool CanScheduleWork() override;
		virtual void LaunchInternal(const TSharedPtr<FTask>& InTask) override;
		virtual void OnEnd(bool bWasCancelled) override;

		virtual void ClearRegistry(const bool bCancel = false) override;

		void ClearGroups();
	};

	// Task group for batched operations
	class PCGEXCORE_API FTaskGroup final : public IAsyncHandleGroup
	{
		friend class FTaskManager;
		friend class FSimpleCallbackTask;
		friend class FScopeIterationTask;
		friend class FForceSingleThreadedScopeIterationTask;

	public:
		using FIterationCallback = std::function<void(const int32, const FScope&)>;
		FIterationCallback OnIterationCallback;

		using FPrepareSubLoopsCallback = std::function<void(const TArray<FScope>&)>;
		FPrepareSubLoopsCallback OnPrepareSubLoopsCallback;

		using FSubLoopStartCallback = std::function<void(const FScope&)>;
		FSubLoopStartCallback OnSubLoopStartCallback;

		explicit FTaskGroup(const FName InName);

		template <typename T, typename... Args>
		void StartRanges(const int32 NumIterations, const int32 ChunkSize, const bool bPrepareOnly, Args&&... InArgs)
		{
			if (!IsAvailable())
			{
				return;
			}

			if (!NumIterations)
			{
				AssertEmptyThread();
				return;
			}

			TArray<FScope> Loops;
			const int32 NumLoops = SubLoopScopes(Loops, NumIterations, FMath::Max(1, GetSanitizedBatchSize(NumIterations, ChunkSize)));

			if (OnPrepareSubLoopsCallback)
			{
				OnPrepareSubLoopsCallback(Loops);
			}

			Launch(NumLoops, [&](int32 i)
			{
				PCGEX_MAKE_SHARED(Task, T, std::forward<Args>(InArgs)...)
				Task->bPrepareOnly = bPrepareOnly;
				Task->Scope = Loops[i];
				return Task;
			});
		}

		void StartIterations(const int32 NumIterations, const int32 ChunkSize, const bool bForceSingleThreaded = false, const bool bPreparationOnly = false);
		void StartSubLoops(const int32 NumIterations, const int32 ChunkSize, const bool bForceSingleThreaded = false);

		void AddSimpleCallback(FSimpleCallback&& InCallback);
		void AddSimpleCallbacks(TArray<FSimpleCallback>&& InCallbacks);
		void StartSimpleCallbacks();

		template <typename T>
		void StartTasksBatch(const TArray<TSharedPtr<T>>& InTasks)
		{
			static_assert(TIsDerivedFrom<T, FTask>::Value, "T must derive from IAsyncHandle");
			const TArray<TSharedPtr<FTask>>& BaseTasks = reinterpret_cast<const TArray<TSharedPtr<FTask>>&>(InTasks);
			StartHandlesBatchImpl(BaseTasks);
		}

	protected:
		TArray<FSimpleCallback> SimpleCallbacks;

		void ExecScopeIteration(const FScope& Scope, bool bPrepareOnly) const;
		void TriggerSimpleCallback(int32 Index);
	};

	PCGEXCORE_API
	void ExecuteOnMainThread(const TSharedPtr<IAsyncHandleGroup>& ParentHandle, FExecuteCallback&& Callback);

	PCGEXCORE_API
	void ExecuteOnMainThread(FExecuteCallback&& Callback);

	PCGEXCORE_API
	void ExecuteOnMainThreadAndWait(FExecuteCallback&& Callback);

	// True when UObject work (spawning actors, NewObject, FindFunction) is currently illegal -- a package save or GC
	// is in progress. Game-thread output phases that marshal object work must defer (re-tick) until this is false.
	// See the PCGEX_DEFER_IF_OBJECT_WORK_BLOCKED macro.
	PCGEXCORE_API
	bool IsObjectWorkBlocked();

// Defer the current bool-returning AdvanceWork (return false = "not complete, re-tick") when UObject work is illegal
// -- a package save / GC is in progress. USE ONLY in a main-thread-only element (PCGEX_ELEMENT_MAIN_THREAD_ONLY):
// on an off-thread/paused context this return-false is never re-driven and would hang. The game-thread UObject work
// itself (spawn / marshal / FindFunction) must still carry its own PCGExMT::IsObjectWorkBlocked() backstop.
#define PCGEX_DEFER_IF_OBJECT_WORK_BLOCKED if (PCGExMT::IsObjectWorkBlocked()) { return false; }

	// Base task class
	class PCGEXCORE_API FTask : public IAsyncHandle
	{
		friend class FTaskManager;
		friend class FTaskGroup;

	public:
		FTask() = default;
		virtual void ExecuteTask(const TSharedPtr<FTaskManager>& TaskManager) = 0;

	protected:
		void Launch(const TSharedPtr<FTask>& InTask, const bool bIsExpected = false) const;
	};

	// Indexed task base
	class PCGEXCORE_API FPCGExIndexedTask : public FTask
	{
	protected:
		int32 TaskIndex;

	public:
		PCGEX_ASYNC_TASK_NAME(FPCGExIndexedTask)

		explicit FPCGExIndexedTask(const int32 InTaskIndex)
			: FTask()
			  , TaskIndex(InTaskIndex)
		{
		}
	};

	// Built-in task types
	class PCGEXCORE_API FSimpleCallbackTask final : public FPCGExIndexedTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FSimpleCallbackTask)

		explicit FSimpleCallbackTask(const int32 InTaskIndex)
			: FPCGExIndexedTask(InTaskIndex)
		{
		}

		virtual void ExecuteTask(const TSharedPtr<FTaskManager>& TaskManager) override;
	};

	class PCGEXCORE_API FScopeIterationTask : public FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FScopeIterationTask)
		bool bPrepareOnly = false;
		FScope Scope = FScope{};
		int32 NumIterations = -1;

		virtual void ExecuteTask(const TSharedPtr<FTaskManager>& TaskManager) override;
	};

	// Main thread execution
	class PCGEXCORE_API IExecuteOnMainThread : public IAsyncHandle
	{
	public:
		FCompletionCallback OnCompleteCallback;

		explicit IExecuteOnMainThread();
		virtual bool Start() override;

	protected:
		double EndTime = 0.0;
		virtual void Schedule();
		virtual bool Execute();
		virtual void OnEnd(bool bWasCancelled) override;
		bool ShouldStop();
	};

	class PCGEXCORE_API FTimeSlicedMainThreadLoop : public IExecuteOnMainThread
	{
	protected:
		FScope Scope = FScope{};

	public:
		using FIterationCallback = std::function<void(const int32, const FScope&)>;
		FIterationCallback OnIterationCallback;

		explicit FTimeSlicedMainThreadLoop(const int32 NumIterations);
		virtual bool Start() override;
		virtual void Cancel() override;

	protected:
		virtual bool Execute() override;
	};
}
