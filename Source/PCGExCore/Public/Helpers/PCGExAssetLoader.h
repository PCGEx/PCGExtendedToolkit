// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Types/PCGExTypes.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

struct FPCGExContext;
struct FStreamableHandle;

namespace PCGExMT
{
	class FTaskManager;
}

namespace PCGExData
{
	class FPointIOCollection;
}

namespace PCGEx
{
	class PCGEXCORE_API IAssetLoader : public TSharedFromThis<IAssetLoader>
	{
	protected:
		TArray<FName> AttributeNames;
		TSet<FSoftObjectPath> UniquePaths;

		TSharedPtr<FStreamableHandle> LoadHandle;
		int8 bFinalized = 0;

		FPCGExContext* Context = nullptr;

	public:
		TSharedPtr<PCGExData::FPointIOCollection> IOCollection;
		TArray<TSharedPtr<TArray<PCGExValueHash>>> Keys;

		IAssetLoader() = default;
		IAssetLoader(FPCGExContext* InContext, const TSharedPtr<PCGExData::FPointIOCollection>& InIOCollection, const TArray<FName>& InAttributeNames);
		virtual ~IAssetLoader();

		virtual bool IsEmpty() const { return true; }

		// Synchronous discovery. For each (IO × attribute) builds an attribute accessor once,
		// fans out per-scope reads via ParallelOrSequential, and merges per-scope unique paths
		// via TScopedSet::Collapse. Populates Keys (per-IO hash array) and UniquePaths.
		// Returns false if no valid paths were discovered.
		bool Discover();

		// Merge discovered UniquePaths into the context's RequiredAssets set so PCG's normal
		// LoadAssets() phase picks them up. Idempotent on empty.
		void AddAssetDependencies();

		// Blocking synchronous load via PCGExHelpers::LoadBlocking_AnyThread. The context tracks
		// the streamable handle. Auto-invokes Finalize() so AssetsMap is ready on return.
		// Returns false if no paths to load or load failed.
		bool Load();

		// Async load via task manager. Used for the legacy standalone flow (not via context deps).
		// Auto-invokes Finalize() when the streamable handle completes.
		bool Load(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager);

		// Validate UniquePaths and populate the typed AssetsMap. Call from the element's
		// PostLoadAssetsDependencies() override (context deps flow). Idempotent.
		virtual void Finalize();

		TSharedPtr<TArray<PCGExValueHash>> GetKeys(const int32 IOIndex);

	protected:
		void FinalizeTracking() const;
		virtual void PrepareFinalize() {}
		virtual void BuildAssetsMap() {}

		// Any thread: objects from the context's tracked loads (see FPCGExContext::ResolveObjects_AnyThread).
		// OutObjects is index-aligned with OutPaths.
		void ResolveUniquePaths(TArray<FSoftObjectPath>& OutPaths, TArray<UObject*>& OutObjects) const;
	};

	template <typename T>
	class TAssetLoader : public IAssetLoader
	{
	public:
		TMap<PCGExValueHash, TObjectPtr<T>> AssetsMap;

		TAssetLoader(FPCGExContext* InContext, const TSharedPtr<PCGExData::FPointIOCollection>& InIOCollection, const TArray<FName>& InAttributeNames)
			: IAssetLoader(InContext, InIOCollection, InAttributeNames)
		{
		}

		virtual bool IsEmpty() const override { return AssetsMap.IsEmpty(); }

		TObjectPtr<T>* GetAsset(const PCGExValueHash Key) { return AssetsMap.Find(Key); }

	protected:
		virtual void PrepareFinalize() override
		{
			AssetsMap.Reserve(UniquePaths.Num());
		}

		virtual void BuildAssetsMap() override
		{
			TArray<FSoftObjectPath> Paths;
			TArray<UObject*> Objects;
			ResolveUniquePaths(Paths, Objects);

			for (int32 i = 0; i < Paths.Num(); i++)
			{
				if (T* Resolved = Cast<T>(Objects[i]))
				{
					AssetsMap.Add(PCGExTypes::ComputeHash(Paths[i]), Resolved);
				}
			}
		}
	};
}
