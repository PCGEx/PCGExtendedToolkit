// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExStreamingHelpers.h"

#include "CoreMinimal.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Core/PCGExContext.h"
#include "PCGExSubSystem.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "UObject/SoftObjectPath.h"

namespace PCGExStreamingHelpers
{
	// File-local helpers for the cached load path. Named (not anonymous / static) so the Unity build can't
	// collide them with same-named helpers in sibling TUs.

	// Wrap a freshly-loaded handle: through the subsystem (which may cache it) when one exists, else standalone.
	PCGExHelpers::FPCGExSharedAssetHandlePtr WrapLoadedBatch(UPCGExSubSystem* Subsystem, const TSharedPtr<FStreamableHandle>& Handle, const bool bCacheMisses)
	{
		if (Subsystem)
		{
			return Subsystem->TrackLoadedBatch(Handle, bCacheMisses);
		}
		if (Handle.IsValid())
		{
			return MakeShared<PCGExHelpers::FPCGExSharedAssetHandle>(Handle, FPlatformTime::Seconds());
		}
		return nullptr;
	}

	// Shared back-end for both LoadAndCacheBlocking_AnyThread overloads.
	TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> LoadAndCacheBlockingSet(const TSet<FSoftObjectPath>& Paths, FPCGExContext* InContext, const bool bCacheMisses)
	{
		TArray<PCGExHelpers::FPCGExSharedAssetHandlePtr> Result;

		// Peek the cache FIRST, off the game thread: a warm hit needs no marshal at all, which is what keeps
		// a cached load deadlock-free under PCG cancellation. Only a real miss touches the game thread.
		UPCGExSubSystem* Subsystem = UPCGExSubSystem::GetSubsystemForCurrentWorld();

		TArray<FSoftObjectPath> Misses;
		if (Subsystem)
		{
			Subsystem->PeekCachedResources(Paths, Result, Misses);
		}
		else
		{
			Misses = Paths.Array();
		}

		if (!Misses.IsEmpty())
		{
			// CALLER RESPONSIBILITY: this miss path marshals-and-waits on the game thread, so the node must
			// be game-thread-affine for the loading phase (e.g. PCGEX_ELEMENT_MAIN_THREAD_ONLY_IN_PREPARE)
			// or it can deadlock under PCG cancellation.
			auto LoadMisses = [&Subsystem, &Misses, bCacheMisses]() -> PCGExHelpers::FPCGExSharedAssetHandlePtr
			{
				const TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestSyncLoad(MoveTemp(Misses));
				return WrapLoadedBatch(Subsystem, Handle, bCacheMisses);
			};

			PCGExHelpers::FPCGExSharedAssetHandlePtr MissWrapper;
			if (IsInGameThread())
			{
				MissWrapper = LoadMisses();
			}
			else
			{
				PCGExMT::ExecuteOnMainThreadAndWait([&]()
				{
					MissWrapper = LoadMisses();
				});
			}

			if (MissWrapper)
			{
				Result.Add(MissWrapper);
			}
		}

		if (InContext)
		{
			for (const PCGExHelpers::FPCGExSharedAssetHandlePtr& Handle : Result)
			{
				InContext->TrackCachedAsset(Handle);
			}
		}

		return Result;
	}
}

namespace PCGExHelpers
{
	TSharedPtr<FStreamableHandle> LoadBlocking_AnyThread(const FSoftObjectPath& Path, FPCGExContext* InContext)
	{
		// UAssetManager is game-thread-only, so off-thread callers dispatch and block. The context tracks
		// the handle to prevent premature GC.
		TSharedPtr<FStreamableHandle> Handle;
		if (IsInGameThread())
		{
			Handle = UAssetManager::GetStreamableManager().RequestSyncLoad(Path);
			if (InContext)
			{
				InContext->TrackAssetsHandle(Handle);
			}
		}
		else
		{
			PCGExMT::ExecuteOnMainThreadAndWait([&]()
			{
				Handle = LoadBlocking_AnyThread(Path, InContext);
			});
		}

		return Handle;
	}

	TSharedPtr<FStreamableHandle> LoadBlocking_AnyThread(const TSharedPtr<TSet<FSoftObjectPath>>& Paths, FPCGExContext* InContext)
	{
		TSharedPtr<FStreamableHandle> Handle;
		if (IsInGameThread())
		{
			Handle = UAssetManager::GetStreamableManager().RequestSyncLoad(Paths->Array());
			if (InContext)
			{
				InContext->TrackAssetsHandle(Handle);
			}
		}
		else
		{
			PCGExMT::ExecuteOnMainThreadAndWait([&]()
			{
				Handle = LoadBlocking_AnyThread(Paths, InContext);
			});
		}
		return Handle;
	}

	FPCGExSharedAssetHandle::~FPCGExSharedAssetHandle()
	{
		// SafeReleaseHandle marshals to the game thread if the last owner drops us off-thread.
		SafeReleaseHandle(Handle);
	}

	bool FPCGExSharedAssetHandle::IsValid() const
	{
		return Handle.IsValid() && Handle->IsActive();
	}

	void FPCGExSharedAssetHandle::GetCoveredPaths(TArray<FSoftObjectPath>& OutPaths) const
	{
		// The handle already owns its requested-asset list; read it back instead of duplicating it.
		if (Handle.IsValid())
		{
			Handle->GetRequestedAssets(OutPaths);
		}
		else
		{
			OutPaths.Reset();
		}
	}

	TArray<FPCGExSharedAssetHandlePtr> LoadAndCacheBlocking_AnyThread(const TSharedPtr<TSet<FSoftObjectPath>>& Paths, FPCGExContext* InContext, const bool bCacheMisses)
	{
		if (!Paths || Paths->IsEmpty())
		{
			return {};
		}
		return PCGExStreamingHelpers::LoadAndCacheBlockingSet(*Paths, InContext, bCacheMisses);
	}

	FPCGExSharedAssetHandlePtr LoadAndCacheBlocking_AnyThread(const FSoftObjectPath& Path, FPCGExContext* InContext, const bool bCacheMisses)
	{
		// Single path: a stack set avoids the heap TSet the TSharedPtr<TSet> overload would allocate.
		TSet<FSoftObjectPath> Paths;
		Paths.Add(Path);

		const TArray<FPCGExSharedAssetHandlePtr> Handles = PCGExStreamingHelpers::LoadAndCacheBlockingSet(Paths, InContext, bCacheMisses);
		return Handles.IsEmpty() ? nullptr : Handles[0];
	}

	void LoadBlockingTracked_AnyThread(const TSharedPtr<TSet<FSoftObjectPath>>& Paths, FPCGExContext* InContext)
	{
		check(InContext);
		// Keep-alive is via the context, so the return is intentionally discarded.
		LoadAndCacheBlocking_AnyThread(Paths, InContext, InContext->WantsResourcesCached());
	}

	void LoadBlockingTracked_AnyThread(const FSoftObjectPath& Path, FPCGExContext* InContext)
	{
		check(InContext);
		LoadAndCacheBlocking_AnyThread(Path, InContext, InContext->WantsResourcesCached());
	}

	void Load(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, FGetPaths&& GetPathsFunc, FOnLoadEnd&& OnLoadEnd)
	{
		check(TaskManager);

		// LoadToken keeps the task manager alive across the async load; it must be released on BOTH the
		// success and early-out paths or the task group's completion count drifts.
		PCGExMT::ExecuteOnMainThread(TaskManager, [GetPathsFunc, OnLoadEnd, TaskManager]()
		{
			TArray<FSoftObjectPath> Paths = GetPathsFunc();

			if (Paths.IsEmpty())
			{
				OnLoadEnd(false, nullptr);
				return;
			}

			TWeakPtr<PCGExMT::FAsyncToken> LoadToken = TaskManager->TryCreateToken(FName("LoadToken"));
			const TSharedPtr<FStreamableHandle> LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				MoveTemp(Paths),
				[OnLoadEnd, LoadToken](TSharedPtr<FStreamableHandle> InHandle) // NOLINT(performance-unnecessary-value-param)
				{
					OnLoadEnd(true, InHandle);
					PCGEX_ASYNC_RELEASE_CAPTURED_TOKEN(LoadToken)
				});

			// Already-completed or failed synchronously (cached assets / invalid paths): the callback won't fire.
			if (!LoadHandle || !LoadHandle->IsActive())
			{
				if (!LoadHandle || !LoadHandle->HasLoadCompleted())
				{
					OnLoadEnd(false, LoadHandle);
				}
				else
				{
					OnLoadEnd(true, LoadHandle);
				}
				PCGEX_ASYNC_RELEASE_TOKEN(LoadToken)
			}
		});
	}

	bool LoadTracked(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, FGetPaths&& GetPathsFunc, FOnTrackedLoadEnd&& OnLoadEnd)
	{
		check(TaskManager);

		// Capture the context's WEAK handle + caching decision now: the raw FPCGExContext* must NOT be touched
		// at async completion, since the context can be destroyed while the load token keeps this callback alive.
		const TWeakPtr<FPCGContextHandle> CtxHandle = TaskManager->GetContext()->GetWeakSelfHandle();
		const bool bCacheMisses = TaskManager->GetContext()->WantsResourcesCached();

		// Peek the cache FIRST, off the game thread: an all-cached request completes here with no game-thread
		// roundtrip at all.
		TArray<FSoftObjectPath> Paths = GetPathsFunc();
		if (Paths.IsEmpty())
		{
			// Empty set -> not-loaded, matching the legacy Load() contract (NOT a behavioral change). Callers
			// that cancel on !bSuccess guard with HasAssetRequirements() first, so they never reach here empty.
			OnLoadEnd(false);
			return true;
		}

		UPCGExSubSystem* Subsystem = UPCGExSubSystem::GetSubsystemForCurrentWorld();

		TArray<FPCGExSharedAssetHandlePtr> Hits;
		TArray<FSoftObjectPath> Misses;
		if (Subsystem)
		{
			TSet<FSoftObjectPath> PathSet;
			PathSet.Append(Paths);
			Subsystem->PeekCachedResources(PathSet, Hits, Misses);
		}
		else
		{
			Misses = MoveTemp(Paths);
		}

		// Register reused hits for keep-alive (skip silently if the context is already gone).
		if (!Hits.IsEmpty())
		{
			FPCGContext::FSharedContext<FPCGExContext> SharedContext(CtxHandle);
			if (FPCGExContext* Ctx = SharedContext.Get())
			{
				for (const FPCGExSharedAssetHandlePtr& Handle : Hits)
				{
					Ctx->TrackCachedAsset(Handle);
				}
			}
		}

		// Fully cached: complete synchronously, no game-thread roundtrip. The async flow must tolerate that.
		if (Misses.IsEmpty())
		{
			OnLoadEnd(true);
			return true;
		}

		// Capture the subsystem WEAKLY: completion can fire many frames later, when the subsystem may be gone
		// (WrapLoadedBatch tolerates null).
		const TWeakObjectPtr<UPCGExSubSystem> WeakSubsystem = Subsystem;
		PCGExMT::ExecuteOnMainThread(TaskManager, [Misses = MoveTemp(Misses), OnLoadEnd, TaskManager, CtxHandle, WeakSubsystem, bCacheMisses]() mutable
		{
			TWeakPtr<PCGExMT::FAsyncToken> LoadToken = TaskManager->TryCreateToken(FName("LoadToken"));

			// Shared by the two mutually-exclusive completion paths below; registers on the context only while
			// it's still alive.
			auto RegisterAndComplete = [OnLoadEnd, CtxHandle, WeakSubsystem, bCacheMisses](const TSharedPtr<FStreamableHandle>& InHandle)
			{
				FPCGContext::FSharedContext<FPCGExContext> SharedContext(CtxHandle);
				if (FPCGExContext* Ctx = SharedContext.Get())
				{
					if (const FPCGExSharedAssetHandlePtr Wrapper = PCGExStreamingHelpers::WrapLoadedBatch(WeakSubsystem.Get(), InHandle, bCacheMisses))
					{
						Ctx->TrackCachedAsset(Wrapper);
					}
				}

				OnLoadEnd(InHandle.IsValid() && InHandle->HasLoadCompleted());
			};

			const TSharedPtr<FStreamableHandle> LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
				MoveTemp(Misses),
				[RegisterAndComplete, LoadToken](TSharedPtr<FStreamableHandle> InHandle) // NOLINT(performance-unnecessary-value-param)
				{
					RegisterAndComplete(InHandle);
					PCGEX_ASYNC_RELEASE_CAPTURED_TOKEN(LoadToken)
				});

			// Synchronously already-completed or failed: the async callback won't fire, so finish here.
			if (!LoadHandle || !LoadHandle->IsActive())
			{
				RegisterAndComplete(LoadHandle);
				PCGEX_ASYNC_RELEASE_TOKEN(LoadToken)
			}
		});

		return false;
	}

	void SafeReleaseHandle(TSharedPtr<FStreamableHandle>& InHandle)
	{
		if (!InHandle.IsValid())
		{
			return;
		}

		if (IsInGameThread())
		{
			InHandle->ReleaseHandle();
			InHandle.Reset();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [Handle = MoveTemp(InHandle)]()
			{
				if (Handle.IsValid())
				{
					Handle->ReleaseHandle();
				}
			});
		}
	}

	void SafeReleaseHandles(TArray<TSharedPtr<FStreamableHandle>>& InHandles)
	{
		if (InHandles.IsEmpty())
		{
			return;
		}

		if (IsInGameThread())
		{
			for (TSharedPtr<FStreamableHandle>& Handle : InHandles)
			{
				if (Handle.IsValid())
				{
					Handle->ReleaseHandle();
				}
			}
			InHandles.Empty();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [Handles = MoveTemp(InHandles)]()
			{
				for (const TSharedPtr<FStreamableHandle>& Handle : Handles)
				{
					if (Handle.IsValid())
					{
						Handle->ReleaseHandle();
					}
				}
			});
		}
	}

}
