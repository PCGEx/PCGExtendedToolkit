// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include <atomic>
#include <functional>

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "Async/Async.h"

struct FPCGExContext;
struct FStreamableHandle;

namespace PCGExMT
{
	class IAsyncHandleGroup;
	class FTaskManager;
}

namespace PCGExHelpers
{
	using FGetPaths = std::function<TArray<FSoftObjectPath>()>;
	using FOnLoadEnd = std::function<void(const bool bSuccess, TSharedPtr<FStreamableHandle> StreamableHandle)>;
	using FOnTrackedLoadEnd = std::function<void(const bool bSuccess)>;

	// RAII keep-alive wrapper around a (possibly batched) FStreamableHandle, shared by the cache and any
	// number of contexts. The handle is never exposed so the last-reference-wins release can't be subverted
	// by an out-of-band ReleaseHandle().
	struct PCGEXCORE_API FPCGExSharedAssetHandle
	{
		// Eviction timestamp (FPlatformTime seconds). Atomic so a cache hit can refresh it without the lock.
		std::atomic<double> LastAccess;

		FPCGExSharedAssetHandle(TSharedPtr<FStreamableHandle> InHandle, const double InLastAccess)
			: LastAccess(InLastAccess), Handle(MoveTemp(InHandle))
		{
		}

		~FPCGExSharedAssetHandle();

		bool IsValid() const;

		// Paths this (batched) handle keeps resident; the cache uses them for its per-path index and eviction.
		void GetCoveredPaths(TArray<FSoftObjectPath>& OutPaths) const;

	private:
		TSharedPtr<FStreamableHandle> Handle;
	};

	using FPCGExSharedAssetHandlePtr = TSharedPtr<FPCGExSharedAssetHandle>;

	PCGEXCORE_API
	TSharedPtr<FStreamableHandle> LoadBlocking_AnyThread(const FSoftObjectPath& Path, FPCGExContext* InContext = nullptr);

	template <typename T>
	static TSharedPtr<FStreamableHandle> LoadBlocking_AnyThreadTpl(const TSoftObjectPtr<T>& SoftObjectPtr, FPCGExContext* InContext = nullptr)
	{
		return LoadBlocking_AnyThread(SoftObjectPtr.ToSoftObjectPath(), InContext);
	}

	PCGEXCORE_API
	TSharedPtr<FStreamableHandle> LoadBlocking_AnyThread(const TSharedPtr<TSet<FSoftObjectPath>>& Paths, FPCGExContext* InContext = nullptr);

	// Cached sibling of LoadBlocking_AnyThread: reuses warm assets and batch-loads only the misses, keeping
	// them warm for next time when bCacheMisses. With InContext, wrappers are registered for keep-alive.
	// A miss marshals to the game thread -- call from a game-thread-affine prep step or it can deadlock
	// under PCG cancellation.
	PCGEXCORE_API
	TArray<FPCGExSharedAssetHandlePtr> LoadAndCacheBlocking_AnyThread(const TSharedPtr<TSet<FSoftObjectPath>>& Paths, FPCGExContext* InContext = nullptr, bool bCacheMisses = true);

	PCGEXCORE_API
	FPCGExSharedAssetHandlePtr LoadAndCacheBlocking_AnyThread(const FSoftObjectPath& Path, FPCGExContext* InContext = nullptr, bool bCacheMisses = true);

	// Context-required, fire-and-forget load: the caller gets no handle (keep-alive is via the context, so
	// it can't release one out from under the cache). Caches misses iff InContext->WantsResourcesCached().
	// CALLER RESPONSIBILITY: a possible miss marshals to the game thread and blocks, so the caller must be
	// game-thread-affine, or it can deadlock under PCG cancellation.
	PCGEXCORE_API
	void LoadBlockingTracked_AnyThread(const FSoftObjectPath& Path, FPCGExContext* InContext);

	PCGEXCORE_API
	void LoadBlockingTracked_AnyThread(const TSharedPtr<TSet<FSoftObjectPath>>& Paths, FPCGExContext* InContext);

	template <typename T>
	static void LoadBlockingTracked_AnyThreadTpl(const TSoftObjectPtr<T>& SoftObjectPtr, FPCGExContext* InContext)
	{
		LoadBlockingTracked_AnyThread(SoftObjectPtr.ToSoftObjectPath(), InContext);
	}

	PCGEXCORE_API
	void Load(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, FGetPaths&& GetPathsFunc, FOnLoadEnd&& OnLoadEnd);

	// Cache-aware, context-owned async sibling of Load(). Caches misses iff the context opted in. Keep-alive
	// is internal via the context's weak handle (so mid-load cancellation is safe), which is why the callback
	// only reports success. Use Load() when there is no context to own the result.
	PCGEXCORE_API
	bool LoadTracked(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager, FGetPaths&& GetPathsFunc, FOnTrackedLoadEnd&& OnLoadEnd);

	PCGEXCORE_API
	void SafeReleaseHandle(TSharedPtr<FStreamableHandle>& InHandle);

	PCGEXCORE_API
	void SafeReleaseHandles(TArray<TSharedPtr<FStreamableHandle>>& InHandles);
}
