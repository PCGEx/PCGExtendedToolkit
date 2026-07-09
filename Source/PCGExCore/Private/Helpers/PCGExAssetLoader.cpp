// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExAssetLoader.h"

#include "PCGExCoreSettingsCache.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExContext.h"
#include "Core/PCGExMT.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExPointIO.h"
#include "Engine/AssetManager.h"
#if WITH_EDITOR
#include "Helpers/PCGDynamicTrackingHelpers.h"
#endif
#include "Helpers/PCGExStreamingHelpers.h"
#include "Types/PCGExTypes.h"

namespace PCGEx
{
	IAssetLoader::IAssetLoader(FPCGExContext* InContext, const TSharedPtr<PCGExData::FPointIOCollection>& InIOCollection, const TArray<FName>& InAttributeNames)
		: AttributeNames(InAttributeNames)
		  , Context(InContext)
		  , IOCollection(InIOCollection)
	{
		Keys.Init(nullptr, InIOCollection->Num());
	}

	IAssetLoader::~IAssetLoader()
	{
		PCGExHelpers::SafeReleaseHandle(LoadHandle);
	}

	bool IAssetLoader::Discover()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IAssetLoader::Discover);

		// Runs entirely inline (no task manager) -- caller is expected to be on a worker thread
		// (typically Boot()), where nested ParallelFor is safe.

		const int32 ChunkSize = PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize();

		// Pre-warm InKeys for each IO: FPointIO::GetInKeys lazily inits under a write lock,
		// so calling it concurrently from the per-scope tasks below would cause every scope
		// after the first to spin on the lock. Touch each IO once up-front while it's cheap.
		PCGExMT::ParallelOrSequential(
			IOCollection->Num(),
			[&](const int32 i)
			{
				(void)IOCollection->Pairs[i]->GetInKeys();
			}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

		for (const TSharedPtr<PCGExData::FPointIO>& PointIO : IOCollection->Pairs)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IAssetLoader::Discover::Iteration);

			if (!PointIO)
			{
				continue;
			}

			TSharedRef<PCGExData::FPointIO> PointIORef = PointIO.ToSharedRef();
			const int32 NumPoints = PointIORef->GetNum();
			if (NumPoints <= 0)
			{
				continue;
			}

			for (const FName& AssetAttributeName : AttributeNames)
			{
				TSharedPtr<PCGExData::TAttributeBroadcaster<FSoftObjectPath>> Broadcaster = MakeShared<PCGExData::TAttributeBroadcaster<FSoftObjectPath>>();
				if (!Broadcaster->Prepare(AssetAttributeName, PointIORef))
				{
					continue;
				}

				TSharedPtr<TArray<PCGExValueHash>> KeysPtr = MakeShared<TArray<PCGExValueHash>>();
				Keys[PointIORef->IOIndex] = KeysPtr;

				// @Data source: one value governs the dataset -- bulk-fill from the broadcaster's cached
				// single value instead of scanning every point. Consumers stay blind to the domain.
				if (Broadcaster->ProcessingInfos.bIsDataDomain)
				{
					const FSoftObjectPath DataPath = Broadcaster->FetchSingle(PCGExData::FElement(0), FSoftObjectPath());
					if (DataPath.IsAsset())
					{
						KeysPtr->Init(PCGExTypes::ComputeHash(DataPath), NumPoints);
						UniquePaths.Add(DataPath);
					}
					else
					{
						KeysPtr->Init(0, NumPoints);
					}

					continue;
				}

				KeysPtr->Init(0, NumPoints);
				TArray<PCGExValueHash>& KeysRef = *KeysPtr.Get();

				TArray<PCGExMT::FScope> Scopes;
				const int32 SanitizedChunk = FMath::Max(1, PCGExMT::GetSanitizedBatchSize(NumPoints, ChunkSize));
				const int32 NumScopes = PCGExMT::SubLoopScopes(Scopes, NumPoints, SanitizedChunk);

				// Asset paths typically dedupe heavily across points (a handful of unique paths
				// per scope), so default-grow the per-scope sets instead of over-reserving.
				PCGExMT::TScopedSet<FSoftObjectPath> ScopedUnique(Scopes, /*ReserveFactor*/ 0);

				PCGExMT::ParallelOrSequential(
					NumScopes,
					[&](const int32 ScopeIdx)
					{
						const PCGExMT::FScope& Scope = Scopes[ScopeIdx];

						// Per-scope inline buffer: stack-resident when Scope.Count <= inline cap
						// (covers chunk sizes ≤256), heap-allocates once per scope otherwise.
						// Peak memory is bounded by chunk × num-threads regardless of NumPoints,
						// instead of scaling linearly with NumPoints.
						TArray<FSoftObjectPath, TInlineAllocator<256>> SliceDump;
						SliceDump.SetNum(Scope.Count);
						Broadcaster->FetchSlice(MakeArrayView(SliceDump), Scope);

						TSet<FSoftObjectPath>& LocalUnique = ScopedUnique.Get_Ref(Scope);
						for (int32 i = 0; i < Scope.Count; ++i)
						{
							const FSoftObjectPath& Path = SliceDump[i];
							if (!Path.IsAsset())
							{
								continue;
							}

							KeysRef[Scope.Start + i] = PCGExTypes::ComputeHash(Path);
							LocalUnique.Add(Path);
						}
					}, /*Threshold=*/2, EParallelForFlags::Unbalanced);

				ScopedUnique.Collapse(UniquePaths);
			}
		}

		return !UniquePaths.IsEmpty();
	}

	void IAssetLoader::AddAssetDependencies()
	{
		if (UniquePaths.IsEmpty() || !Context)
		{
			return;
		}

		TSet<FSoftObjectPath>& Required = Context->GetRequiredAssets();
		Required.Append(UniquePaths);
	}

	bool IAssetLoader::Load()
	{
		if (UniquePaths.IsEmpty())
		{
			return false;
		}

		// LoadBlocking_AnyThread marshals to the game thread internally and registers the handle
		// against the context's TrackedAssets. We also hold our own ref so the assets stay
		// resident for the lifetime of the loader, independent of the context's cleanup order.
		TSharedPtr<TSet<FSoftObjectPath>> PathsHandle = MakeShared<TSet<FSoftObjectPath>>(UniquePaths);
		LoadHandle = PCGExHelpers::LoadBlocking_AnyThread(PathsHandle, Context);

		Finalize();
		return !IsEmpty();
	}

	bool IAssetLoader::Load(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
	{
		if (UniquePaths.IsEmpty())
		{
			return false;
		}

		PCGExHelpers::Load(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE]() -> TArray<FSoftObjectPath>
			{
				PCGEX_ASYNC_THIS_RET({})
				return This->UniquePaths.Array();
			},
			[PCGEX_ASYNC_THIS_CAPTURE](const bool bSuccess, TSharedPtr<FStreamableHandle> StreamableHandle)
			{
				PCGEX_ASYNC_THIS
				This->LoadHandle = StreamableHandle;
				if (bSuccess)
				{
					This->Finalize();
				}
			});

		return true;
	}

	void IAssetLoader::Finalize()
	{
		// Single-CAS gate: if multiple threads race here (e.g. an async Load() completion
		// callback and a manual Finalize() from the element), exactly one wins.
		if (FPlatformAtomics::InterlockedCompareExchange(&bFinalized, 1, 0) != 0)
		{
			return;
		}

		PrepareFinalize();
		BuildAssetsMap();
		FinalizeTracking();
	}

	TSharedPtr<TArray<PCGExValueHash>> IAssetLoader::GetKeys(const int32 IOIndex)
	{
		return Keys[IOIndex];
	}

	void IAssetLoader::FinalizeTracking() const
	{
#if WITH_EDITOR
		if (Context)
		{
			FPCGDynamicTrackingHelper DynamicTracking;
			DynamicTracking.EnableAndInitialize(Context, UniquePaths.Num());
			for (const FSoftObjectPath& Path : UniquePaths)
			{
				DynamicTracking.AddToTracking(FPCGSelectionKey::CreateFromPath(Path), /*bIsCulled=*/ false);
			}
			DynamicTracking.Finalize(Context);
		}
#endif
	}
}
