// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"
#include "PCGExClusterCache.generated.h"

struct FPCGExGeo2DProjectionDetails;

namespace PCGExClusters
{
	class FCluster;
}

UENUM()
enum class EClusterCacheType : uint8
{
	PreBuild,
	// Configured in GraphBuilderDetails, built at compile time
	Opportunistic // Computed by processors, cached for downstream reuse
};

namespace PCGExClusters
{
	/**
	 * Base class for all cached cluster data.
	 * Derive from this to store computed data that can be reused across processors.
	 */
	class PCGEXCORE_API ICachedClusterData
	{
	public:
		virtual ~ICachedClusterData() = default;

		/** Context hash for validation (e.g., projection settings hash). 0 = always valid. */
		uint32 ContextHash = 0;

		/** Derived from the node and edge arrays alone, never from vtx positions: a mirror sharing both arrays inherits it. */
		bool bTopologyOnly = false;
	};

	/**
	 * Build context passed to cache factories during pre-build phase.
	 */
	struct PCGEXCORE_API FClusterCacheBuildContext
	{
		TSharedRef<FCluster> Cluster;

		/** Native settings (set by caller based on GraphBuilderDetails) */
		const FPCGExGeo2DProjectionDetails* Projection = nullptr;

		explicit FClusterCacheBuildContext(const TSharedRef<FCluster>& InCluster)
			: Cluster(InCluster)
		{
		}
	};

	/**
	 * Factory interface for creating cached data.
	 * Register factories with FClusterCacheRegistry to enable pre-build and opportunistic caching.
	 */
	class PCGEXCORE_API IClusterCacheFactory : public TSharedFromThis<IClusterCacheFactory>
	{
	public:
		virtual ~IClusterCacheFactory() = default;

		/** Unique key for this cache type */
		virtual FName GetCacheKey() const = 0;

		/** Human-readable name for UI */
		virtual FText GetDisplayName() const = 0;

		/** Tooltip describing what this cache contains */
		virtual FText GetTooltip() const = 0;

		/** Whether this is a pre-build or opportunistic cache */
		virtual EClusterCacheType GetCacheType() const = 0;

		/**
		 * Build the cached data from a cluster.
		 * For opportunistic caches, this may return nullptr (processors build directly).
		 */
		virtual TSharedPtr<ICachedClusterData> Build(const FClusterCacheBuildContext& Context) const = 0;
	};

	/**
	 * Registry singleton for cluster cache factories.
	 * Thread-safe for concurrent access from multiple modules.
	 */
	class PCGEXCORE_API FClusterCacheRegistry
	{
	public:
		static FClusterCacheRegistry& Get();

		void Register(const TSharedRef<IClusterCacheFactory>& Factory);
		void Unregister(FName Key);

		IClusterCacheFactory* GetFactory(FName Key) const;
		void GetPreBuildKeys(TArray<FName>& OutKeys) const;
		void GetOpportunisticKeys(TArray<FName>& OutKeys) const;
		void GetAllFactories(TArray<TSharedRef<IClusterCacheFactory>>& OutFactories) const;

	private:
		FClusterCacheRegistry() = default;
		TMap<FName, TSharedRef<IClusterCacheFactory>> Factories;
		mutable FRWLock Lock;
	};
}
