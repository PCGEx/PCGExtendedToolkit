// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Data/PCGExDataCommon.h"

class UPCGParamData;
struct FPCGExContext;

namespace PCGExData
{
	class FPointIO;
	class FPointIOCollection;
}

namespace PCGExMT
{
	template <typename T>
	class TScopedArray;
}

namespace PCGExBucketDispatchHelpers
{
	using FCreateIOFn = TFunctionRef<
		TSharedPtr<PCGExData::FPointIO>(
			const TSharedRef<PCGExData::FPointIOCollection>& InCollection,
			PCGExData::EIOInit InitMode)>;

	/**
	 * Dispatch pre-bucketed point indices to their output collections, choosing the optimal IO
	 * init mode:
	 *   - If every point landed in a single bucket, Forward (zero-copy) that bucket's output.
	 *   - Otherwise, create a New output per non-empty bucket and copy the indexed points in via
	 *     PCGExPointArrayDataHelpers::SetNumPointsAllocated + InheritProperties.
	 *
	 * Layout the caller must uphold:
	 *   - Counts and ScopedIndices each have Buckets.Num() + 1 entries; the trailing slot is the
	 *     "unmatched" bucket (points that didn't classify into any named Buckets).
	 *   - All Buckets entries are non-null; each bucket's FPointIOCollection has already pre-sized
	 *     its Pairs array (typically Init(nullptr, NumPairs) in element setup).
	 *   - UnmatchedBucket may be null when the caller doesn't want to emit unmatched points; the
	 *     unmatched slot of Counts/ScopedIndices is still tracked (just discarded here).
	 *   - CreateIO is the caller's per-batch IO factory; it must place the new PointIO into the
	 *     correct slot of the collection's Pairs array and return it.
	 */
	PCGEXCORE_API void DispatchBuckets(
		TConstArrayView<TSharedPtr<PCGExData::FPointIOCollection>> Buckets,
		const TSharedPtr<PCGExData::FPointIOCollection>& UnmatchedBucket,
		TConstArrayView<int32> Counts,
		TConstArrayView<TSharedPtr<PCGExMT::TScopedArray<int32>>> ScopedIndices,
		int32 NumPoints,
		FCreateIOFn CreateIO);

	using FCreateRawIOFn = TFunctionRef<
		TSharedPtr<PCGExData::FPointIO>(
			const TSharedRef<PCGExData::FPointIOCollection>& InCollection)>;

	/** Build a new managed param data holding the given rows of InSource (entry keys, order preserved). */
	PCGEXCORE_API UPCGParamData* ExtractParamRows(FPCGExContext* InContext, const UPCGParamData* InSource, TConstArrayView<int32> InRows);

	/**
	 * Param-data counterpart of DispatchBuckets, same bucket layout contract. Each non-empty bucket
	 * becomes a row-subset param data staged through its IO's output override; if every row landed in a
	 * single bucket, the original param data is forwarded untouched (zero-copy).
	 * CreateIO must place a NoInit FPointIO into the collection's Pairs slot and return it.
	 */
	PCGEXCORE_API void DispatchParamBuckets(
		FPCGExContext* InContext,
		const UPCGParamData* InSourceParam,
		TConstArrayView<TSharedPtr<PCGExData::FPointIOCollection>> Buckets,
		const TSharedPtr<PCGExData::FPointIOCollection>& UnmatchedBucket,
		TConstArrayView<int32> Counts,
		TConstArrayView<TSharedPtr<PCGExMT::TScopedArray<int32>>> ScopedIndices,
		int32 NumRows,
		FCreateRawIOFn CreateIO);
}
