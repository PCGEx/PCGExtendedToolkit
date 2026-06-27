// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include <atomic>

#include "Helpers/PCGExStreamingHelpers.h"

#include "PCGExSubSystem.generated.h"

#define PCGEX_SUBSYSTEM UPCGExSubSystem* PCGExSubsystem = UPCGExSubSystem::GetSubsystemForCurrentWorld(); check(PCGExSubsystem)

class UPCGComponent;
class UPCGExConstantFilterFactory;

namespace PCGExPointFilter
{
	class IFilter;
}

class UPCGExGridIDTracker;

UENUM()
enum class EPCGExSubsystemEventType : uint8
{
	None       = 0 UMETA(Hidden),
	Regenerate = 1 UMETA(DisplayName = "Regenerate", Tooltip="Triggers regeneration on subcribers."),
	DataUpdate = 2 UMETA(DisplayName = "Data Update", Tooltip="Triggers a data update event."),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGlobalEvent, UPCGComponent*, Source, EPCGExSubsystemEventType, EventType, uint32, EventId);

namespace PCGEx
{
	struct PCGEXCORE_API FPolledEvent
	{
		UPCGComponent* Source = nullptr;
		EPCGExSubsystemEventType Type = EPCGExSubsystemEventType::None;
		uint32 EventId = 0;

		FPolledEvent() = default;

		FPolledEvent(UPCGComponent* InSource, const EPCGExSubsystemEventType InType, const uint32 InEventId)
			: Source(InSource)
			  , Type(InType)
			  , EventId(InEventId)
		{
		}

		~FPolledEvent() = default;

		bool operator==(const FPolledEvent& Other) const
		{
			return Source == Other.Source && Type == Other.Type && EventId == Other.EventId;
		}

		FORCEINLINE friend uint32 GetTypeHash(const FPolledEvent& Key)
		{
			return HashCombineFast(static_cast<uint32>(Key.Type), HashCombineFast(GetTypeHash(Key.Source), Key.EventId));
		}
	};
}

UCLASS()
class PCGEXCORE_API UPCGExSubSystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

	FRWLock SubsystemLock;
	FRWLock IndexBufferLock;
	FRWLock BeaconsLock;

public:
	UPCGExSubSystem();

	UPROPERTY(BlueprintAssignable, Category = "Delegates")
	FOnGlobalEvent OnGlobalEvent;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** To be used when a PCG component can not have a world anymore, to unregister itself. */
	static UPCGExSubSystem* GetSubsystemForCurrentWorld();

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaSeconds) override;

	virtual bool IsTickableInEditor() const override
	{
		return true;
	}

	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** Will return the subsystem from the World if it exists and if it is initialized */
	static UPCGExSubSystem* GetInstance(UWorld* World);

	/** Adds an action that will be executed once at the beginning of this subsystem's next Tick(). */
	using FTickAction = TFunction<void()>;
	void RegisterBeginTickAction(FTickAction&& Action);

	void PollEvent(UPCGComponent* InSource, EPCGExSubsystemEventType InEventType, uint32 InEventId);

#pragma region Indices buffer

protected:
	void EnsureIndexBufferSize(const int32 Count);

public:
	TArrayView<const int32> GetIndexRange(const int32 Start, const int32 Count);


#pragma endregion

	FORCEINLINE double GetEndTime() const
	{
		return EndTime;
	}

protected:
	double EndTime = 0.0;
	TArray<int32> IndexBuffer;
	bool bWantsTick = false;

	/** Functions will be executed at the beginning of the tick and then removed from this array. */

	TArray<FTickAction> BeginTickActions;
	TSet<PCGEx::FPolledEvent> PolledEvents;

	const IConsoleVariable* CVarEditorTimePerFrame = nullptr;
	const IConsoleVariable* CVarTimePerFrame = nullptr;
	double GetTickBudgetInSeconds();

	void ExecuteBeginTickActions();

#pragma region Resource cache

public:
	// Read-only cache partition (no load): cache hits go to OutHits as reused wrappers (LastAccess
	// refreshed), everything else to OutMisses. Lock-only, safe from any thread -- callers peek here first
	// and only marshal a miss load to the game thread.
	void PeekCachedResources(const TSet<FSoftObjectPath>& InPaths, TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr>& OutHits, TArray<FSoftObjectPath>& OutMisses);

	// Wraps an already-loaded streamable handle and, when bInsert is true, adds it to the warm set / index.
	// Lock-only (no RequestSyncLoad), so it's safe from any thread -- e.g. a RequestAsyncLoad completion
	// that may not run on the game thread. Returns the wrapper (for context keep-alive registration).
	PCGExHelpers::FPCGExSharedAssetHandlePtr TrackLoadedBatch(const TSharedPtr<FStreamableHandle>& InHandle, bool bInsert);

	// Drops every cached handle from the warm set immediately (game thread). Assets still referenced by a
	// live context survive until that context releases them. Called on Deinitialize.
	void ClearResourceCache();

	// Optional eviction guard (off by default -- see pcgex.ResourceCache.SuspendEvictionWhileGenerating):
	// while the active-generation count is > 0 the sweep is suppressed so long-running graphs keep their
	// cache warm regardless of TTL. Wire these from execution start/end if/when needed.
	void NotifyGenerationStarted() { ActiveGenerationCount.fetch_add(1, std::memory_order_acq_rel); }
	void NotifyGenerationEnded() { ActiveGenerationCount.fetch_sub(1, std::memory_order_acq_rel); }

protected:
	// Timer-gated eviction: sweeps only every pcgex.ResourceCache.SweepInterval seconds so entries survive
	// the many-frame gap between a producing node and a far-downstream consumer.
	void TickResourceCache();
	void SweepResourceCache(const double Now);

	mutable FRWLock ResourceCacheLock;

	// Per-path discovery index. Weak on purpose: it must NOT keep handles alive, so it never inflates the
	// shared refcount the sweep reads to tell "still in use" from "cache-only". Many paths from one batch
	// map to the same wrapper.
	TMap<FSoftObjectPath, TWeakPtr<PCGExHelpers::FPCGExSharedAssetHandle>> ResourceCacheIndex;

	// Strong keep-alive owner -- the cache's warm set. Dropping an entry releases its batch handle iff no
	// context still references it.
	TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> ResourceCacheWarm;

	// Mirrors ResourceCacheWarm.Num() for a lock-free IsTickable() check, so the subsystem keeps ticking
	// while the cache is non-empty (and stops once it drains).
	std::atomic<int32> CachedHandleCount{0};
	std::atomic<int32> ActiveGenerationCount{0};
	double LastCacheSweepTime = 0.0;

	// Set once any path ends up covered by more than one warm wrapper (concurrent multi-source loads).
	// Gates the eviction sweep's index re-point pass so the common single-owner case stays cheap.
	// Only ever read/written under ResourceCacheLock.
	bool bResourceIndexHasDuplicates = false;

#pragma endregion
};
