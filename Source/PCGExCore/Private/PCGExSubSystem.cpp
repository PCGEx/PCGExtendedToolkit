// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExSubSystem.h"

#include "PCGExLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ObjectTools.h"
#else
#include "Engine/Engine.h"
#include "Engine/World.h"
#endif

namespace PCGExResourceCacheCVars
{
	TAutoConsoleVariable<float> CVarSweepInterval(
		TEXT("pcgex.ResourceCache.SweepInterval"),
		5.0f,
		TEXT("Seconds between PCGEx streamable-resource cache eviction sweeps."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarTTL(
		TEXT("pcgex.ResourceCache.TTL"),
		45.0f,
		TEXT("Seconds an idle (unreferenced) cached PCGEx resource survives before it is evicted."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarSuspendEvictionWhileGenerating(
		TEXT("pcgex.ResourceCache.SuspendEvictionWhileGenerating"),
		false,
		TEXT("When enabled, suppresses resource-cache eviction while any PCG generation is in flight."),
		ECVF_Default);
}

UPCGExSubSystem::UPCGExSubSystem()
	: Super()
{
}

void UPCGExSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UPCGExSubSystem::Deinitialize()
{
	ClearResourceCache();
	Super::Deinitialize();
}

UPCGExSubSystem* UPCGExSubSystem::GetSubsystemForCurrentWorld()
{
	UWorld* World = nullptr;

#if WITH_EDITOR
	if (GEditor)
	{
		if (GEditor->PlayWorld)
		{
			World = GEditor->PlayWorld;
		}
		else
		{
			World = GEditor->GetEditorWorldContext().World();
		}
	}
	else
#endif
		if (GEngine)
		{
			World = GEngine->GetCurrentPlayWorld();
		}

	return GetInstance(World);
}

void UPCGExSubSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ExecuteBeginTickActions();
	TickResourceCache();
}

ETickableTickType UPCGExSubSystem::GetTickableTickType() const
{
	return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional;
}

bool UPCGExSubSystem::IsTickable() const
{
	return bWantsTick || CachedHandleCount.load(std::memory_order_relaxed) > 0;
}

TStatId UPCGExSubSystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPCGExSubsystem, STATGROUP_Tickables);
}

UPCGExSubSystem* UPCGExSubSystem::GetInstance(UWorld* World)
{
	if (World)
	{
		return World->GetSubsystem<UPCGExSubSystem>();
	}
	return nullptr;
}

void UPCGExSubSystem::RegisterBeginTickAction(FTickAction&& Action)
{
	FWriteScopeLock WriteScopeLock(SubsystemLock);
	bWantsTick = true;
	BeginTickActions.Emplace(Action);
}

void UPCGExSubSystem::PollEvent(UPCGComponent* InSource, const EPCGExSubsystemEventType InEventType, const uint32 InEventId)
{
	FWriteScopeLock WriteScopeLock(SubsystemLock);
	bWantsTick = true;
	PolledEvents.Add(PCGEx::FPolledEvent(InSource, InEventType, InEventId));
}

// Shared identity-mapped index buffer (IndexBuffer[i] == i) used across the system
// to avoid redundant allocations. Double-checked locking: fast read-only check first,
// then re-check under write lock before growing. Over-allocates by 1024 to reduce
// contention from frequent small growth requests.
void UPCGExSubSystem::EnsureIndexBufferSize(const int32 Count)
{
	{
		FReadScopeLock ReadScopeLock(IndexBufferLock);
		if (IndexBuffer.Num() >= Count)
		{
			return;
		}
	}
	{
		FWriteScopeLock WriteScopeLock(IndexBufferLock);
		if (IndexBuffer.Num() >= Count)
		{
			return;
		}

		const int32 StartIndex = IndexBuffer.Num();
		IndexBuffer.SetNumUninitialized(Count + 1024);
		for (int i = StartIndex; i < Count; i++)
		{
			IndexBuffer[i] = i;
		}
	}
}

TArrayView<const int32> UPCGExSubSystem::GetIndexRange(const int32 Start, const int32 Count)
{
	EnsureIndexBufferSize(Start + Count);
	return TArrayView<const int32>(IndexBuffer.GetData() + Start, Count);
}

double UPCGExSubSystem::GetTickBudgetInSeconds()
{
	float Val = 5000.0;

#if WITH_EDITOR
	if (GEditor && !GEditor->IsPlaySessionInProgress())
	{
		if (!CVarEditorTimePerFrame)
		{
			CVarEditorTimePerFrame = IConsoleManager::Get().FindConsoleVariable(TEXT("pcg.EditorFrameTime"));
		}
		if (CVarEditorTimePerFrame)
		{
			Val = CVarEditorTimePerFrame->GetFloat();
		}
	}
	else
#endif
	{
		if (!CVarTimePerFrame)
		{
			CVarTimePerFrame = IConsoleManager::Get().FindConsoleVariable(TEXT("pcg.FrameTime"));
		}
		if (CVarTimePerFrame)
		{
			Val = CVarTimePerFrame->GetFloat();
		}
	}

	return FMath::Max(Val, 1.0) / 1000.0;
}

// Drain-and-execute: move all pending actions and events out of the locked
// containers, then execute them outside the lock. This minimizes lock hold
// time and allows actions to safely enqueue new work for the next tick.
void UPCGExSubSystem::ExecuteBeginTickActions()
{
	EndTime = FPlatformTime::Seconds() + GetTickBudgetInSeconds();

	TArray<FTickAction> Actions;
	TArray<PCGEx::FPolledEvent> Events;

	{
		FWriteScopeLock WriteScopeLock(SubsystemLock);
		bWantsTick = false;

		Actions = MoveTemp(BeginTickActions);
		BeginTickActions.Reset();

		Events = PolledEvents.Array();
		PolledEvents.Reset();
	}

	for (const PCGEx::FPolledEvent& Event : Events)
	{
		OnGlobalEvent.Broadcast(Event.Source, Event.Type, Event.EventId);
	}
	for (FTickAction& Action : Actions)
	{
		Action();
	}
}

#pragma region Resource cache

void UPCGExSubSystem::PeekCachedResources(const TSet<FSoftObjectPath>& InPaths, TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr>& OutHits, TArray<FSoftObjectPath>& OutMisses)
{
	const double Now = FPlatformTime::Seconds();

	// Many requested paths map to the same batch wrapper; dedup hits by wrapper identity in O(1) each
	// rather than OutHits.AddUnique's linear scan (assumes OutHits starts empty, as all callers pass it).
	TSet<const PCGExHelpers::FPCGExSharedAssetHandle*> Seen;

	// Resolve hits against the per-path index; everything else is a miss for the caller to load.
	FReadScopeLock ReadLock(ResourceCacheLock);
	for (const FSoftObjectPath& Path : InPaths)
	{
		if (const TWeakPtr<PCGExHelpers::FPCGExSharedAssetHandle>* Found = ResourceCacheIndex.Find(Path))
		{
			if (PCGExHelpers::FPCGExSharedAssetHandlePtr Pinned = Found->Pin())
			{
				Pinned->LastAccess.store(Now, std::memory_order_relaxed);
				bool bAlreadySeen = false;
				Seen.Add(Pinned.Get(), &bAlreadySeen);
				if (!bAlreadySeen) { OutHits.Add(Pinned); }
				continue;
			}
		}
		OutMisses.Add(Path);
	}
}

PCGExHelpers::FPCGExSharedAssetHandlePtr UPCGExSubSystem::TrackLoadedBatch(const TSharedPtr<FStreamableHandle>& InHandle, const bool bInsert)
{
	// Never wrap a failed load: a dead wrapper would only be handed back and discarded, and if cached it
	// would keep returning an invalid handle. All callers null-check the result.
	if (!InHandle.IsValid()) { return nullptr; }

	const PCGExHelpers::FPCGExSharedAssetHandlePtr Wrapper = MakeShared<PCGExHelpers::FPCGExSharedAssetHandle>(InHandle, FPlatformTime::Seconds());

	// Read-only callers skip insertion -- reuse the wrapper without caching it.
	if (!bInsert) { return Wrapper; }

	// Index from the handle's authoritative path list (single source of truth, post null/dup strip).
	TArray<FSoftObjectPath> Covered;
	Wrapper->GetCoveredPaths(Covered);

	{
		FWriteScopeLock WriteLock(ResourceCacheLock);
		ResourceCacheWarm.Add(Wrapper);
		for (const FSoftObjectPath& Path : Covered)
		{
			// Keep the first wrapper indexed for a path. A later wrapper that also covers Path stays in the
			// warm set (still reachable: the eviction sweep re-points the index to it if the first wrapper is
			// dropped). Flag the duplicate so the sweep knows it must run that re-point pass.
			if (ResourceCacheIndex.Contains(Path)) { bResourceIndexHasDuplicates = true; }
			else { ResourceCacheIndex.Add(Path, Wrapper); }
		}

		// Count mutated under the same lock as the warm set, so the two never disagree across a concurrent
		// ClearResourceCache or eviction sweep.
		CachedHandleCount.fetch_add(1, std::memory_order_acq_rel);
	}

	return Wrapper;
}

void UPCGExSubSystem::TickResourceCache()
{
	if (CachedHandleCount.load(std::memory_order_acquire) <= 0) { return; }

	const double Now = FPlatformTime::Seconds();
	const double Interval = FMath::Max(0.1, static_cast<double>(PCGExResourceCacheCVars::CVarSweepInterval.GetValueOnGameThread()));

	if ((Now - LastCacheSweepTime) < Interval) { return; }

	LastCacheSweepTime = Now;
	SweepResourceCache(Now);
}

void UPCGExSubSystem::SweepResourceCache(const double Now)
{
	// Optional guard: keep everything warm while a generation is running, regardless of TTL.
	if (PCGExResourceCacheCVars::CVarSuspendEvictionWhileGenerating.GetValueOnGameThread()
		&& ActiveGenerationCount.load(std::memory_order_acquire) > 0)
	{
		return;
	}

	const double TTL = FMath::Max(0.0, static_cast<double>(PCGExResourceCacheCVars::CVarTTL.GetValueOnGameThread()));

	// Collect evicted wrappers and let them release OUTSIDE the lock -- their destructor releases the
	// streamable handle, which we keep off the cache critical section.
	TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> Evicted;
	int32 RemainingWarm = 0;

	{
		FWriteScopeLock WriteLock(ResourceCacheLock);

		// Paths whose index entry pointed at an evicted wrapper. Only collected when duplicate coverage
		// exists; otherwise an evicted wrapper's paths have no other warm owner and are simply removed.
		TSet<FSoftObjectPath> OrphanedPaths;

		for (int32 i = ResourceCacheWarm.Num() - 1; i >= 0; --i)
		{
			const PCGExHelpers::FPCGExSharedAssetHandlePtr& Handle = ResourceCacheWarm[i];

			// Refcount 1 == only the warm array holds it (no live context). Reading the count through a
			// reference (not a copy) avoids adding a transient reference of our own.
			const bool bUnused = !Handle.IsValid() || Handle.GetSharedReferenceCount() <= 1;
			const bool bIdle = !Handle.IsValid() || (Now - Handle->LastAccess.load(std::memory_order_relaxed)) > TTL;

			if (!bUnused || !bIdle) { continue; }

			if (Handle.IsValid())
			{
				// Drop the index entries still pointing at THIS wrapper. Paths come straight from the
				// streamable handle (no stored copy) -- the same list we indexed at load.
				TArray<FSoftObjectPath> Covered;
				Handle->GetCoveredPaths(Covered);
				for (const FSoftObjectPath& Path : Covered)
				{
					if (const TWeakPtr<PCGExHelpers::FPCGExSharedAssetHandle>* Found = ResourceCacheIndex.Find(Path))
					{
						if (Found->Pin() == Handle)
						{
							ResourceCacheIndex.Remove(Path);
							if (bResourceIndexHasDuplicates) { OrphanedPaths.Add(Path); }
						}
					}
				}
			}

			Evicted.Add(MoveTemp(ResourceCacheWarm[i]));
			ResourceCacheWarm.RemoveAtSwap(i);
		}

		// Re-point any orphaned path that a surviving warm wrapper still covers: a multi-loaded path must
		// stay discoverable as long as ANY warm wrapper backs it. OrphanedPaths is empty unless a duplicate
		// was ever indexed, so the common single-owner sweep skips this survivor scan entirely.
		if (!OrphanedPaths.IsEmpty())
		{
			for (const PCGExHelpers::FPCGExSharedAssetHandlePtr& Survivor : ResourceCacheWarm)
			{
				if (!Survivor.IsValid()) { continue; }
				TArray<FSoftObjectPath> Covered;
				Survivor->GetCoveredPaths(Covered);
				for (const FSoftObjectPath& Path : Covered)
				{
					if (OrphanedPaths.Contains(Path) && !ResourceCacheIndex.Contains(Path))
					{
						ResourceCacheIndex.Add(Path, Survivor);
					}
				}
			}
		}

		// Count mutated under the same lock as the warm set so the two never disagree.
		if (!Evicted.IsEmpty())
		{
			CachedHandleCount.fetch_sub(Evicted.Num(), std::memory_order_acq_rel);
		}
		RemainingWarm = ResourceCacheWarm.Num();
	}

	if (!Evicted.IsEmpty())
	{
		UE_LOG(LogPCGEx, Log, TEXT("PCGEx resource cache: swept %d handle(s) (%d still warm)."), Evicted.Num(), RemainingWarm);
	}

	// Evicted wrappers release here -- on the game thread, outside the lock.
	Evicted.Reset();
}

void UPCGExSubSystem::ClearResourceCache()
{
	TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> Evicted;

	{
		FWriteScopeLock WriteLock(ResourceCacheLock);
		ResourceCacheIndex.Empty();
		Evicted = MoveTemp(ResourceCacheWarm);
		ResourceCacheWarm.Reset();
		bResourceIndexHasDuplicates = false;

		// Count zeroed under the same lock as the warm set.
		CachedHandleCount.store(0, std::memory_order_release);
	}

	// Release outside the lock. Wrappers still referenced by a live context survive until it drops them.
	Evicted.Reset();
}

#pragma endregion
