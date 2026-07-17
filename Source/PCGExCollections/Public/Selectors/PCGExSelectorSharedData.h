// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExAssetCollection.h"

class UPCGExSelectorFactoryData;

namespace PCGExData
{
	class FPointIOCollection;
}

namespace PCGExCollections
{
	class FSelectorSharedDataCache;

	/**
	 * Opaque base for collection-derived state that's invariant across facades.
	 * Each selector that benefits from sharing subclasses this with its specific cached arrays
	 * (e.g. per-entry volume/extents, per-entry resolved range bounds, sort orders, etc.).
	 *
	 * Instances are constructed by UPCGExSelectorFactoryData::BuildSharedData when a
	 * cache miss occurs. Once returned from the cache, instances are read-only; multiple
	 * ops read them concurrently without synchronization.
	 */
	class PCGEXCOLLECTIONS_API FSelectorSharedData : public TSharedFromThis<FSelectorSharedData>
	{
	public:
		virtual ~FSelectorSharedData() = default;

		/**
		 * Called once per instance, under the cache lock, after BuildSharedData and before the
		 * instance is published to any facade -- the only sanctioned place to finalize
		 * batch-dependent state (e.g. Quota's Proportion budget vs InCache.AllInputsPointCount),
		 * since the lock removes any dependency on facade init order. Composites must forward
		 * to their children. NOT called when the cache is bypassed (self-built shared data);
		 * keep a functional fallback for that path.
		 */
		virtual void OnCached(const FSelectorSharedDataCache& InCache)
		{
		}
	};

	/**
	 * Context-scoped cache of FSelectorSharedData instances, keyed by (factory, category).
	 *
	 * Lifetime: owned by a consumer context (e.g. UPCGExStagingDistributeContext) alongside
	 * FPickPacker. Dies with the context -- no staleness across graph runs.
	 *
	 * Thread safety: GetOrBuild locks a critical section for the map insert. Shared data
	 * objects themselves are read-only after construction.
	 */
	class PCGEXCOLLECTIONS_API FSelectorSharedDataCache : public TSharedFromThis<FSelectorSharedDataCache>
	{
	public:
		/**
		 * Total In-side point count across the consumer's batch, or -1 when unknown. Set once
		 * at Boot, BEFORE any GetOrBuild call; consumed by OnCached implementations that scale
		 * state to the full batch (-1 -> they fall back to per-facade behavior).
		 */
		int64 AllInputsPointCount = -1;

		/**
		 * In-side point count per input, indexed by FPointIO::IOIndex. Set once at Boot
		 * alongside AllInputsPointCount. Consumed by AllInputs-scoped state that pre-splits
		 * global budgets deterministically across inputs (Quota max caps). Empty -> unknown;
		 * dependents fall back to their shared-counter behavior.
		 */
		TArray<int64> PerInputPointCounts;

		/**
		 * Fill AllInputsPointCount and PerInputPointCounts from the consumer's main input
		 * collection. Call once at Boot, BEFORE any GetOrBuild call.
		 */
		void SetBatchPointCounts(const PCGExData::FPointIOCollection& InCollection);

		/**
		 * Return cached shared data for (Factory, Target), building it lazily on first access.
		 * Freshly built instances receive OnCached(*this) under the lock before publication.
		 * Returns null if the factory declines to produce shared data (its BuildSharedData returns null).
		 */
		TSharedPtr<FSelectorSharedData> GetOrBuild(
			const UPCGExSelectorFactoryData* Factory,
			const UPCGExAssetCollection* Collection,
			const PCGExAssetCollection::FCategory* Target);

#if WITH_EDITOR
		/** Test-only: number of BuildSharedData calls performed by this cache. */
		int32 GetBuildCount() const
		{
			return BuildCount;
		}
#endif

	private:
		using FKey = TPair<const UPCGExSelectorFactoryData*, const PCGExAssetCollection::FCategory*>;

		FCriticalSection Mutex;
		TMap<FKey, TSharedPtr<FSelectorSharedData>> Entries;

#if WITH_EDITOR
		int32 BuildCount = 0;
#endif
	};
}
