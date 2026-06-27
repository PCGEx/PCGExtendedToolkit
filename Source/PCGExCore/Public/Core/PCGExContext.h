// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
//#include "CollisionQueryParams.h"
//#include "Engine/HitResult.h"

#include "PCGContext.h"
#include "PCGExCommon.h"
#include "PCGExMT.h"

#include "Data/PCGExDataCommon.h"

namespace PCGExData
{
	class IBufferProxyPool;
}

class FPCGExUniqueNameGenerator;

namespace PCGExMT
{
	class FAsyncToken;
}

class UPCGExInstancedFactory;
class UPCGExSettings;
class UPCGComponent;
class IPCGExElement;
class UPCGManagedComponent;
struct FStreamableHandle;
struct FAttachmentTransformRules;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGEx
{
	class FManagedObjects;
	class FWorkHandle;
}

namespace PCGExHelpers
{
	struct FPCGExSharedAssetHandle;
}

struct PCGEXCORE_API FPCGExContext : FPCGContext
{
	friend class IPCGExElement;

protected:
	mutable FRWLock StagingLock;
	TArray<FPCGTaggedData> StagedData;

	mutable FRWLock AsyncLock;
	mutable FRWLock AssetsLock;

	TSharedPtr<PCGEx::FWorkHandle> WorkHandle;
	const IPCGExElement* ElementHandle = nullptr;

	// This context's own PCG handle, captured once at construction. Read it via GetWeakSelfHandle(),
	// never GetOrCreateHandle(): after Release() nulls the handle, GetOrCreateHandle() would resurrect
	// a second one bound to the released context and trip ~FPCGContext's ensure(!Handle).
	TWeakPtr<FPCGContextHandle> SelfHandle;

	// Counts in-flight async tasks pinning this context. Read only by the cancel-time game-thread
	// finalizer (see CancelExecution) to know when destruction can be deferred onto the game thread.
	TSharedPtr<PCGExMT::FAsyncContextPinTracker> AsyncPinTracker;

	int32 LoopIndex = INDEX_NONE;
	int32 TopLoopIndex = INDEX_NONE;

	bool bPreparationDispatchedOffThread = false;
	bool bExecutionDispatchedOffThread = false;
	bool bWantsResourcesCached = false;

public:
	TWeakPtr<PCGEx::FWorkHandle> GetWorkHandle()
	{
		return WorkHandle;
	}

	// Non-creating handle accessor. Returns an invalid weak ptr once the context is released -- the
	// "context is gone, bail out" signal for the PCGEX_SHARED_CONTEXT* guards. Use for all self-pinning.
	const TWeakPtr<FPCGContextHandle>& GetWeakSelfHandle() const
	{
		return SelfHandle;
	}

	// Shared pin counter; async task bodies bracket their pinned region with an FAsyncContextPinScope
	// built from this.
	const TSharedPtr<PCGExMT::FAsyncContextPinTracker>& GetAsyncPinTracker() const
	{
		return AsyncPinTracker;
	}

	TSharedPtr<PCGEx::FManagedObjects> ManagedObjects;

	int32 GetLoopIndex() const
	{
		return LoopIndex;
	}

	bool IsExecutingInsideLoop() const
	{
		return TopLoopIndex != INDEX_NONE;
	}

	bool IsRuntimeGen() const;

	bool bScopedAttributeGet = false;
	bool bPropagateAbortedExecution = false;

	// True when the most recent LoadAssets() completed synchronously because every dependency was
	// already resident (a warm resource-cache hit) rather than dispatching an async load. Lets
	// PostLoadAssetsDependencies overrides distinguish a warm re-run from a cold load.
	bool bWarmDependencies = false;

	FPCGExContext();

	// Non-copyable / non-movable: SelfHandle and the base Handle both bind to THIS instance (the
	// handle stores `this` and owns the context), so a copy would dangle or double-delete.
	FPCGExContext(const FPCGExContext&) = delete;
	FPCGExContext& operator=(const FPCGExContext&) = delete;
	FPCGExContext(FPCGExContext&&) = delete;
	FPCGExContext& operator=(FPCGExContext&&) = delete;

	virtual ~FPCGExContext() override;

	UPCGExInstancedFactory* RegisterInstancedFactory(UPCGExInstancedFactory* BaseOperation, const FName OverridePinLabel = NAME_None);

#pragma region Output Data

	bool bFlattenOutput = false;

	void IncreaseStagedOutputReserve(const int32 InIncreaseNum);
	void StageOutput(UPCGData* InData, const FName& InPin, const PCGExData::EStaging Staging = PCGExData::EStaging::None, const TSet<FString>& InTags = {});

#pragma endregion

	UWorld* GetWorld() const;
	const UPCGComponent* GetComponent() const;
	UPCGComponent* GetMutableComponent() const;

#pragma region State

	TSharedPtr<PCGExMT::FTaskManager> GetTaskManager();

	void PauseContext();
	void UnpauseContext();

	void SetState(const PCGExCommon::ContextState StateId);

	virtual bool IsWaitingForTasks();
	void ReadyForExecution();

	bool IsState(const PCGExCommon::ContextState StateId) const
	{
		return CurrentState.load(std::memory_order_acquire) == StateId.GetComparisonIndex().ToUnstableInt();
	}

	bool IsInitialExecution() const
	{
		return IsState(PCGExCommon::States::State_InitialExecution);
	}

	bool IsDone() const
	{
		return IsState(PCGExCommon::States::State_Done);
	}

	bool IsWorkCompleted() const
	{
		return bWorkCompleted.load(std::memory_order_acquire);
	}

	// Executes AdvanceWork with re-entry protection and deferred completion handling
	bool DriveAdvanceWork(const UPCGExSettings* InSettings);

	bool IsWorkCancelled() const
	{
		return bWorkCancelled.load(std::memory_order_acquire) || (TaskManager && TaskManager->IsCancelled()) || !WorkHandle.IsValid();
	}

	bool IsAdvanceWorkInProgress() const
	{
		return bAdvanceWorkInProgress.load(std::memory_order_acquire);
	}

	void SetPendingAsyncEnd()
	{
		bPendingAsyncWorkEnd.store(true, std::memory_order_release);
	}

	void Done();

	bool TryComplete(const bool bForce = false);

protected:
	//~ Execution Flow Atomics
	//~
	//~ Two paths can drive AdvanceWork:
	//~   1. ExecuteInternal (spin loop when using NoPause policy)
	//~   2. OnAsyncWorkEnd (callback when async tasks complete)
	//~
	//~ Flow: AdvanceWork -> async tasks -> OnAsyncWorkEnd -> AdvanceWork (if not spin-looping)
	//~       When spin-looping, OnAsyncWorkEnd skips AdvanceWork; spin loop picks up state changes.
	//~
	//~ Completion: AdvanceWork -> Done() -> TryComplete() -> OnComplete() -> OutputData populated

	std::atomic<uint32> CurrentState{0};             // State machine position (see PCGExCommon::States)
	std::atomic<bool> bPendingAsyncWorkEnd{false};   // Async completed while DriveAdvanceWork was active
	std::atomic<bool> bWorkCompleted{false};         // Set once in TryComplete; triggers OnComplete
	std::atomic<bool> bWorkCancelled{false};         // Cancellation flag; checked throughout execution
	std::atomic<bool> bAdvanceWorkInProgress{false}; // DriveAdvanceWork is active; prevents concurrent driving

	TSharedPtr<PCGExMT::FTaskManager> TaskManager;

	void OnAsyncWorkEnd(const bool bWasCancelled);

	virtual void OnComplete();

#pragma endregion

#pragma region Async resource management

public:
	TSet<FSoftObjectPath>& GetRequiredAssets();

	bool HasAssetRequirements() const
	{
		return RequiredAssets && !RequiredAssets->IsEmpty();
	}

	virtual void RegisterAssetDependencies();
	void AddAssetDependency(const FSoftObjectPath& Dependency);
	// Returns true when an asset-dependency load was initiated (state advanced to
	// State_LoadingAssetDependencies), false when there was nothing to load. On return, bWarmDependencies
	// reflects whether the load completed synchronously from the resource cache (no async yield needed).
	bool LoadAssets();

	void TrackAssetsHandle(const TSharedPtr<FStreamableHandle>& InHandle);

	// Keep-alive registration for cached resource wrappers (LoadAndCacheBlocking_AnyThread). Held for the
	// context's lifetime and dropped on destruction; the underlying handle is released only once the last
	// owner (here or the subsystem cache) lets go.
	void TrackCachedAsset(const TSharedPtr<PCGExHelpers::FPCGExSharedAssetHandle>& InHandle);

	FORCEINLINE bool WantsResourcesCached() const
	{
		return bWantsResourcesCached;
	}

protected:
	TSharedPtr<TSet<FSoftObjectPath>> RequiredAssets;
	TArray<TSharedPtr<FStreamableHandle>> TrackedAssets;
	TArray<TSharedPtr<PCGExHelpers::FPCGExSharedAssetHandle>> TrackedCachedAssets;

#pragma endregion

#pragma region Managed Components

public:
	UPCGManagedComponent* AttachManagedComponent(AActor* InParent, UActorComponent* InComponent, const FAttachmentTransformRules& AttachmentRules) const;

#pragma endregion

protected:
	TSet<FName> ConsumableAttributesSet;
	TSet<FName> ProtectedAttributesSet;

	mutable FRWLock ConsumableAttributesLock;
	mutable FRWLock ProtectedAttributesLock;

public:
	bool bCleanupConsumableAttributes = false;
	void AddConsumableAttributeName(FName InName);
	void AddProtectedAttributeName(FName InName);

	TSharedPtr<FPCGExUniqueNameGenerator> UniqueNameGenerator;

	void EDITOR_TrackPath(const FSoftObjectPath& Path, bool bIsCulled = false);
	void EDITOR_TrackClass(const TSubclassOf<UObject>& InSelectionClass, bool bIsCulled = false);
	void EDITOR_TrackPCGComponentData(const UPCGComponent* InComponent, bool bIsCulled = false);

	bool CanExecute() const;
	bool bQuietInvalidInputWarning = false;

	bool bQuietMissingAttributeError = false;
	bool bQuietMissingInputError = false;
	bool bQuietCancellationError = false;
	bool bWantsDataStealing = false;

	TSharedPtr<PCGExData::IBufferProxyPool> BufferProxyPool;

	virtual bool CancelExecution(const FString& InReason = FString());

protected:
	mutable FRWLock NotifyActorsLock;

	// Actors to notify when execution is complete
	TSet<AActor*> NotifyActors;

	void ExecuteOnNotifyActors(const TArray<FName>& FunctionNames);
	virtual void AddExtraStructReferencedObjects(FReferenceCollector& Collector) override;

	TArray<UPCGExInstancedFactory*> ProcessorOperations;
	TSet<UPCGExInstancedFactory*> InternalOperations;

public:
	void AddNotifyActor(AActor* InActor);
};
