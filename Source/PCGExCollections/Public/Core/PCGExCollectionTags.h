// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCollectionsCommon.h"
#include "Core/PCGExAssetCollection.h"

/**
 * Entry-tag filtering primitives behind the selector tag filter.
 *
 * FTagIndex   -- dense FName -> bit id over a pool's entries, one stride-word mask per raw entry.
 * Predicate   -- three clauses compiled against an index; the compiled form is also the canonical
 *                dedupe key, so equivalent authored lists ("A,B" / "B, A" / "A,B,B") share one pool.
 * FTagPoolStore -- lock-guarded cache of indices and derived pools, keyed by base pool pointer, so
 *                every facade of an execution shares the same pool objects (Quota keys on them).
 */
namespace PCGExCollections::Tags
{
	class PCGEXCOLLECTIONS_API FTagIndex
	{
	public:
		TMap<FName, int32> TagToId;
		int32 NumTags = 0;

		/** int64 words per mask -- (NumTags + 63) / 64. Zero when no entry carries a tag. */
		int32 Stride = 0;

		/** Raw entry count covered by Words (max raw index in the source pool + 1). */
		int32 NumRaw = 0;

		/** Masks, [RawIndex * Stride + Word]. Entries without tags read as all-zero. */
		TArray<int64> Words;

		/**
		 * Build over a pool's entries. InTagSources is EPCGExAssetTagInheritance bits: Asset reads
		 * Entry->Tags, Collection adds the sub-collection's CollectionTags for sub-collection entries.
		 * Other bits are ignored. Ids are assigned in entry order, so the index is deterministic.
		 */
		void Build(const PCGExAssetCollection::FCategory& InPool, uint8 InTagSources);

		FORCEINLINE int32 FindId(const FName InTag) const
		{
			const int32* Id = TagToId.Find(InTag);
			return Id ? *Id : INDEX_NONE;
		}

		/** Mask of a raw entry, or null when the index has no words at all (Stride == 0). */
		FORCEINLINE const int64* GetWords(const int32 InRawIndex) const
		{
			return (Stride > 0 && InRawIndex >= 0 && InRawIndex < NumRaw) ? Words.GetData() + InRawIndex * Stride : nullptr;
		}
	};

	/**
	 * Compiled three-clause predicate. Word arrays are either empty (clause inactive) or Stride long.
	 * An unsatisfiable predicate is normalized to no words and no clauses, so every "matches nothing"
	 * predicate is one key.
	 */
	struct PCGEXCOLLECTIONS_API FCompiledTagPredicate
	{
		enum EClause : uint8
		{
			ClauseRequireAll = 0x1,
			ClauseRequireAny = 0x2,
			ClauseExclude    = 0x4,
		};

		TArray<int64> RequireAllWords;
		TArray<int64> RequireAnyWords;
		TArray<int64> ExcludeWords;
		uint8 ActiveClauses = 0;
		bool bUnsatisfiable = false;

		/** No constraint at all: a derived pool would equal its base. */
		FORCEINLINE bool IsEmpty() const
		{
			return ActiveClauses == 0 && !bUnsatisfiable;
		}

		/** EntryWords may be null only when Stride is 0. */
		bool Test(const int64* EntryWords, int32 Stride) const;

		bool operator==(const FCompiledTagPredicate& Other) const;

		FORCEINLINE bool operator!=(const FCompiledTagPredicate& Other) const
		{
			return !(*this == Other);
		}
	};

	/** Hash consistent with FCompiledTagPredicate::operator==; makes the predicate a map key. */
	PCGEXCOLLECTIONS_API uint32 GetTypeHash(const FCompiledTagPredicate& In);

	/**
	 * Append the tags authored in one operand value. None and blank values contribute nothing. With
	 * bCommaSeparated the value is split on commas, trimmed, empties dropped; otherwise it is one tag.
	 */
	PCGEXCOLLECTIONS_API void ParseOperand(FName InValue, bool bCommaSeparated, TArray<FName>& OutTags);

	/**
	 * Compile three operand lists against an index. Unknown tags (no entry in the index carries them):
	 * RequireAll -> unsatisfiable; RequireAny -> ignored, unsatisfiable only when every tag is unknown;
	 * Exclude -> ignored, clause inactive when every tag is unknown. Duplicates collapse into the masks.
	 */
	PCGEXCOLLECTIONS_API void Compile(
		const FTagIndex& InIndex,
		TConstArrayView<FName> InRequireAll,
		TConstArrayView<FName> InRequireAny,
		TConstArrayView<FName> InExclude,
		FCompiledTagPredicate& OutPredicate);

	/** Filtered copy of InBase (same name, base entry order), compiled. Empty when nothing passes. */
	PCGEXCOLLECTIONS_API TSharedPtr<PCGExAssetCollection::FCategory> BuildDerivedPool(
		const PCGExAssetCollection::FCategory& InBase,
		const FTagIndex& InIndex,
		const FCompiledTagPredicate& InPredicate);

	/**
	 * Execution-scoped cache of tag indices and derived pools. Embedded in FSelectorSharedDataCache
	 * (shared across facades) or owned by a helper when no cache is wired. Every access locks; this
	 * is Init-time only. Returned objects are immutable.
	 */
	class PCGEXCOLLECTIONS_API FTagPoolStore
	{
	public:
		/** Index over InPool's entries for the given tag sources, built once per (pool, sources). */
		TSharedPtr<const FTagIndex> GetOrBuildIndex(const TSharedPtr<PCGExAssetCollection::FCategory>& InPool, uint8 InTagSources);

		/**
		 * Derived pool for (base, index, predicate), built once. An empty predicate returns InBase
		 * itself -- no copy. May return an empty pool; callers decide what an empty pool means.
		 */
		TSharedPtr<PCGExAssetCollection::FCategory> GetOrBuildPool(
			const TSharedPtr<PCGExAssetCollection::FCategory>& InBase,
			const TSharedPtr<const FTagIndex>& InIndex,
			const FCompiledTagPredicate& InPredicate);

		/** Number of derived pools built so far (aliases of a base pool are not counted). */
		int32 NumPools() const;

	private:
		struct FPoolKey
		{
			const PCGExAssetCollection::FCategory* Base = nullptr;
			const FTagIndex* Index = nullptr;
			FCompiledTagPredicate Predicate;

			bool operator==(const FPoolKey& Other) const
			{
				return Base == Other.Base && Index == Other.Index && Predicate == Other.Predicate;
			}

			friend uint32 GetTypeHash(const FPoolKey& In)
			{
				return HashCombine(HashCombine(PointerHash(In.Base), PointerHash(In.Index)), GetTypeHash(In.Predicate));
			}
		};

		mutable FCriticalSection Mutex;
		TMap<TPair<const PCGExAssetCollection::FCategory*, uint8>, TSharedPtr<const FTagIndex>> Indices;
		TMap<FPoolKey, TSharedPtr<PCGExAssetCollection::FCategory>> Pools;
	};
}
