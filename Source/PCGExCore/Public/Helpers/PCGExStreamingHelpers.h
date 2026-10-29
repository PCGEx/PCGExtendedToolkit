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

		// Game thread only: records the loaded objects, since the handle's own accessors are game-thread-only.
		FPCGExSharedAssetHandle(TSharedPtr<FStreamableHandle> InHandle, const double InLastAccess);

		~FPCGExSharedAssetHandle();

		bool IsValid() const;

		// Paths this (batched) handle keeps resident; the cache uses them for its per-path index and eviction.
		void GetCoveredPaths(TArray<FSoftObjectPath>& OutPaths) const;

		// Any thread, no engine lookup. Null when Path is not covered or did not load.
		UObject* FindLoadedObject(const FSoftObjectPath& Path) const;

		// Any thread. Immutable after construction; failed loads are not recorded.
		const TMap<FSoftObjectPath, TWeakObjectPtr<UObject>>& GetLoadedObjects() const { return LoadedObjects; }

	private:
		TSharedPtr<FStreamableHandle> Handle;
		TMap<FSoftObjectPath, TWeakObjectPtr<UObject>> LoadedObjects;
	};

	using FPCGExSharedAssetHandlePtr = TSharedPtr<FPCGExSharedAssetHandle>;

	// Soft-path lookup that is legal on any thread: off the game thread it holds FGCScopeGuard for the lookup only,
	// because StaticFindObjectFast is Fatal while GC holds the hash tables. Each call takes the guard and the global
	// UObject hash lock -- resolve each unique path once, never per point.
	PCGEXCORE_API
	UObject* ResolveObject_AnyThread(const FSoftObjectPath& Path);

	// Same, for a class that must derive from MetaClass.
	PCGEXCORE_API
	UClass* ResolveClass_AnyThread(const FSoftObjectPath& Path, const UClass* MetaClass);

	// Batched: duplicates resolve once and the guard is held per chunk. OutObjects is index-aligned with Paths, so
	// per-row arrays can be passed as-is.
	PCGEXCORE_API
	void ResolveObjects_AnyThread(TConstArrayView<FSoftObjectPath> Paths, TArray<UObject*>& OutObjects);

	// Objects the given handles recorded (no engine lookup), else the batched guarded resolve: for code that holds
	// its own handles instead of a context. Live-world and transient objects are never loaded, so they fall back.
	PCGEXCORE_API
	void ResolveObjects_AnyThread(TConstArrayView<FPCGExSharedAssetHandlePtr> Handles, TConstArrayView<FSoftObjectPath> Paths, TArray<UObject*>& OutObjects);

	// Fills the null entries of InOutObjects (index-aligned with Paths) with the batched guarded resolve.
	PCGEXCORE_API
	void ResolveMissingObjects_AnyThread(TConstArrayView<FSoftObjectPath> Paths, TArray<UObject*>& InOutObjects);

	template <typename T>
	static T* ResolveObject_AnyThreadTpl(const TSoftObjectPtr<T>& SoftObjectPtr)
	{
		return Cast<T>(ResolveObject_AnyThread(SoftObjectPtr.ToSoftObjectPath()));
	}

	template <typename T>
	static UClass* ResolveClass_AnyThreadTpl(const TSoftClassPtr<T>& SoftClassPtr)
	{
		return ResolveClass_AnyThread(SoftClassPtr.ToSoftObjectPath(), T::StaticClass());
	}

	// Every loader below skips paths already resolving to an object of a live (initialized) world: no package
	// load can produce those, and a streamable handle on one would pin that world. Such paths count as
	// loaded, so a request made only of them completes successfully with a null handle.
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
