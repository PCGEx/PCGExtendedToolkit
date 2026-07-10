// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Core/PCGExAssetCollection.h"
#include "Factories/PCGExOperation.h"

namespace PCGExData
{
	class FFacade;
}

struct FPCGExContext;
class UPCGExAssetCollection;

namespace PCGExCollections
{
	class FSelectorSharedData;
}

/**
 * Polymorphic base for per-scope mutable scratch passed into Pick(). Subclassed by selectors
 * that need growable per-pick buffers (match lists, cumulative weights, per-axis value cache,
 * etc.). Owned by the consumer (e.g. FProcessor in StagingDistribute) for the duration of a
 * processing scope; one scratch per (op × worker) so Pick() can mutate it without locking.
 *
 * Selectors that don't need scratch ignore the parameter and never override CreateScratchForScope.
 */
class PCGEXCOLLECTIONS_API FPCGExPickerScratchBase
{
public:
	virtual ~FPCGExPickerScratchBase() = default;
};

/**
 * Per-entry availability view for quota-constrained picking. A thin, inlineable window over
 * an atomic remaining-capacity block owned by a quota decorator (see Selector Modifier : Quota).
 *
 * INDEXING IS CATEGORY-LOCAL: EntryIndex is a position in Target->Entries, NOT a raw
 * collection index -- selector candidate loops already iterate that space.
 *
 * Semantics: negative = uncapped, 0 = unavailable (exhausted or authored 0), positive =
 * capacity left. Reads are relaxed atomic loads -- an L1 read per candidate; the view is
 * only ever constructed on the opted-in quota path, so unconstrained picking pays nothing.
 */
struct FPCGExPickAvailability
{
	/** Remaining capacity per entry, parallel to Target->Entries. Never null when handed to PickFiltered. */
	const std::atomic<int32>* Remaining = nullptr;

	FORCEINLINE bool IsAvailable(const int32 EntryIndex) const
	{
		return Remaining[EntryIndex].load(std::memory_order_relaxed) != 0;
	}
};

/**
 * Abstract hot-path operation for picking an entry from a collection's category.
 *
 * One operation is bound to exactly one FCategory* target -- either Cache->Main or a
 * named sub-category. FSelectorHelper creates one op for Main and one per named
 * category at init, then routes per-point picks based on the point's Category attribute.
 *
 * Concrete subclasses implement Pick() with a tight, branch-minimal body.
 */
class PCGEXCOLLECTIONS_API FPCGExEntryPickerOperation : public FPCGExOperation
{
public:
	/** The category to pick from. Bound once at PrepareForData time; const in the hot path. */
	PCGExAssetCollection::FCategory* Target = nullptr;

	/**
	 * The collection the bound category belongs to. Bound once at PrepareForData time.
	 * Ops that need to resolve per-entry properties via FPCGExAssetCollectionEntry::GetResolvedProperty
	 * consume this during PrepareForData to pre-compute per-entry state.
	 */
	const UPCGExAssetCollection* OwningCollection = nullptr;

	/**
	 * Optional collection-derived shared data. When non-null, the op reads shared state from this
	 * cached instance instead of rebuilding it inline. Set by FSelectorHelper prior to PrepareForData
	 * when a FSelectorSharedDataCache is wired on the consumer context. Null means self-build.
	 */
	TSharedPtr<PCGExCollections::FSelectorSharedData> SharedData;

	/**
	 * Bind the operation to a data facade, a category target, and the owning collection.
	 * @return false if the target is null or otherwise unusable.
	 */
	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InDataFacade, PCGExAssetCollection::FCategory* InTarget, const UPCGExAssetCollection* InOwningCollection);

	/**
	 * Produce a per-scope scratch instance for this op. Called by the consumer (e.g. FProcessor)
	 * once per processing scope, before entering the parallel point loop. The returned scratch
	 * is passed back into Pick() on every call within that scope and may be mutated freely there.
	 *
	 * Default returns nullptr -- ops that don't need scratch leave this alone.
	 * @param MaxPointsInScope Upper bound the consumer expects to process in this scope; ops can size buffers accordingly.
	 */
	virtual TSharedPtr<FPCGExPickerScratchBase> CreateScratchForScope(int32 MaxPointsInScope) const
	{
		return nullptr;
	}

	/**
	 * Pick a raw Entries-array index from the bound target. Returns -1 if no valid pick.
	 * Called per-point in parallel scopes -- must be thread-safe and free of mutation on any
	 * shared state. Scratch is owned by the caller and is the only mutable surface available
	 * to the op during the call; ops that don't need it ignore the parameter.
	 */
	virtual int32 Pick(int32 PointIndex, int32 Seed, FPCGExPickerScratchBase* Scratch = nullptr) const = 0;

	/**
	 * Quota-aware pick: like Pick, but entries reported unavailable by InAvailability must not
	 * be returned. Returns -1 when nothing available satisfies the selector's criteria.
	 *
	 * ONLY invoked by quota decorators (Selector Modifier : Quota) -- consumers and selectors that don't
	 * opt in never execute this path, so it costs nothing when unused. Same thread-safety
	 * contract as Pick.
	 *
	 * Default implementation is a bounded veto+retry around Pick() with salted seeds: correct
	 * for randomized selectors, degrades to "exhausted" (-1) for deterministic ones. Override
	 * for exclusion-aware picking (skip unavailable entries inside the candidate loop) -- all
	 * built-in selectors do.
	 */
	virtual int32 PickFiltered(int32 PointIndex, int32 Seed, const FPCGExPickAvailability& InAvailability, FPCGExPickerScratchBase* Scratch = nullptr) const;
};
