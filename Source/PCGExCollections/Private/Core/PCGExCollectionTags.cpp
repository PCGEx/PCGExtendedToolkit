// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExCollectionTags.h"

#include "Helpers/PCGExArrayHelpers.h"

namespace PCGExCollections::Tags
{
	namespace PCGExCollectionTagsInternal
	{
		FORCEINLINE void SetBit(TArray<int64>& Words, const int32 BitId)
		{
			Words[BitId >> 6] |= (static_cast<int64>(1) << (BitId & 63));
		}

		uint32 HashWords(const TArray<int64>& Words)
		{
			return Words.IsEmpty() ? 0 : FCrc::MemCrc32(Words.GetData(), Words.Num() * sizeof(int64));
		}
	}

#pragma region FTagIndex

	void FTagIndex::Build(const PCGExAssetCollection::FCategory& InPool, const uint8 InTagSources)
	{
		TagToId.Reset();
		Words.Reset();
		NumTags = 0;
		Stride = 0;
		NumRaw = 0;

		const bool bAsset = (InTagSources & static_cast<uint8>(EPCGExAssetTagInheritance::Asset)) != 0;
		const bool bCollection = (InTagSources & static_cast<uint8>(EPCGExAssetTagInheritance::Collection)) != 0;

		const int32 Num = InPool.Entries.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			NumRaw = FMath::Max(NumRaw, InPool.Indices[i] + 1);
		}

		// Two passes: ids first (deterministic, entry order), then masks once Stride is known.
		auto ForEachTag = [&](const FPCGExAssetCollectionEntry* Entry, auto&& Fn)
		{
			if (bAsset)
			{
				for (const FName& Tag : Entry->Tags)
				{
					Fn(Tag);
				}
			}
			if (bCollection && Entry->HasValidSubCollection())
			{
				for (const FName& Tag : Entry->GetSubCollectionPtr()->CollectionTags)
				{
					Fn(Tag);
				}
			}
		};

		for (int32 i = 0; i < Num; ++i)
		{
			ForEachTag(InPool.Entries[i], [&](const FName Tag)
			{
				if (!Tag.IsNone() && !TagToId.Contains(Tag))
				{
					TagToId.Add(Tag, NumTags++);
				}
			});
		}

		if (NumTags == 0)
		{
			return;
		}

		Stride = (NumTags + 63) / 64;
		Words.SetNumZeroed(NumRaw * Stride);

		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Raw = InPool.Indices[i];
			TArrayView<int64> Mask(Words.GetData() + Raw * Stride, Stride);
			ForEachTag(InPool.Entries[i], [&](const FName Tag)
			{
				if (const int32* Id = TagToId.Find(Tag))
				{
					Mask[*Id >> 6] |= (static_cast<int64>(1) << (*Id & 63));
				}
			});
		}
	}

#pragma endregion

#pragma region FCompiledTagPredicate

	bool FCompiledTagPredicate::Test(const int64* EntryWords, const int32 Stride) const
	{
		if (bUnsatisfiable)
		{
			return false;
		}

		// A null mask means "no tags": every word reads as zero.
		if (ActiveClauses & ClauseRequireAll)
		{
			for (int32 i = 0; i < Stride; ++i)
			{
				const int64 W = EntryWords ? EntryWords[i] : 0;
				if ((W & RequireAllWords[i]) != RequireAllWords[i])
				{
					return false;
				}
			}
		}

		if (ActiveClauses & ClauseRequireAny)
		{
			bool bAny = false;
			for (int32 i = 0; i < Stride && !bAny; ++i)
			{
				const int64 W = EntryWords ? EntryWords[i] : 0;
				bAny = (W & RequireAnyWords[i]) != 0;
			}
			if (!bAny)
			{
				return false;
			}
		}

		if (ActiveClauses & ClauseExclude)
		{
			for (int32 i = 0; i < Stride; ++i)
			{
				const int64 W = EntryWords ? EntryWords[i] : 0;
				if ((W & ExcludeWords[i]) != 0)
				{
					return false;
				}
			}
		}

		return true;
	}

	bool FCompiledTagPredicate::operator==(const FCompiledTagPredicate& Other) const
	{
		return ActiveClauses == Other.ActiveClauses
			&& bUnsatisfiable == Other.bUnsatisfiable
			&& RequireAllWords == Other.RequireAllWords
			&& RequireAnyWords == Other.RequireAnyWords
			&& ExcludeWords == Other.ExcludeWords;
	}

	uint32 GetTypeHash(const FCompiledTagPredicate& In)
	{
		// Unqualified GetTypeHash here names this namespace's overload, so hash the scalars directly.
		uint32 Hash = HashCombine(static_cast<uint32>(In.ActiveClauses), In.bUnsatisfiable ? 1u : 0u);
		Hash = HashCombine(Hash, PCGExCollectionTagsInternal::HashWords(In.RequireAllWords));
		Hash = HashCombine(Hash, PCGExCollectionTagsInternal::HashWords(In.RequireAnyWords));
		Hash = HashCombine(Hash, PCGExCollectionTagsInternal::HashWords(In.ExcludeWords));
		return Hash;
	}

#pragma endregion

	void ParseOperand(const FName InValue, const bool bCommaSeparated, TArray<FName>& OutTags)
	{
		if (InValue.IsNone())
		{
			return;
		}

		if (!bCommaSeparated)
		{
			OutTags.Add(InValue);
			return;
		}

		for (const FString& Part : PCGExArrayHelpers::GetStringArrayFromCommaSeparatedList(InValue.ToString()))
		{
			OutTags.Emplace(*Part);
		}
	}

	void Compile(
		const FTagIndex& InIndex,
		const TConstArrayView<FName> InRequireAll,
		const TConstArrayView<FName> InRequireAny,
		const TConstArrayView<FName> InExclude,
		FCompiledTagPredicate& OutPredicate)
	{
		OutPredicate = FCompiledTagPredicate{};
		const int32 Stride = InIndex.Stride;

		if (!InRequireAll.IsEmpty())
		{
			OutPredicate.ActiveClauses |= FCompiledTagPredicate::ClauseRequireAll;
			OutPredicate.RequireAllWords.SetNumZeroed(Stride);
			for (const FName& Tag : InRequireAll)
			{
				const int32 Id = InIndex.FindId(Tag);
				if (Id == INDEX_NONE)
				{
					OutPredicate.bUnsatisfiable = true;
				}
				else
				{
					PCGExCollectionTagsInternal::SetBit(OutPredicate.RequireAllWords, Id);
				}
			}
		}

		if (!InRequireAny.IsEmpty())
		{
			OutPredicate.ActiveClauses |= FCompiledTagPredicate::ClauseRequireAny;
			OutPredicate.RequireAnyWords.SetNumZeroed(Stride);
			bool bAnyKnown = false;
			for (const FName& Tag : InRequireAny)
			{
				const int32 Id = InIndex.FindId(Tag);
				if (Id != INDEX_NONE)
				{
					PCGExCollectionTagsInternal::SetBit(OutPredicate.RequireAnyWords, Id);
					bAnyKnown = true;
				}
			}
			if (!bAnyKnown)
			{
				OutPredicate.bUnsatisfiable = true;
			}
		}

		if (!InExclude.IsEmpty())
		{
			TArray<int64> ExcludeWords;
			ExcludeWords.SetNumZeroed(Stride);
			bool bAnyKnown = false;
			for (const FName& Tag : InExclude)
			{
				const int32 Id = InIndex.FindId(Tag);
				if (Id != INDEX_NONE)
				{
					PCGExCollectionTagsInternal::SetBit(ExcludeWords, Id);
					bAnyKnown = true;
				}
			}
			if (bAnyKnown)
			{
				OutPredicate.ActiveClauses |= FCompiledTagPredicate::ClauseExclude;
				OutPredicate.ExcludeWords = MoveTemp(ExcludeWords);
			}
		}

		// One canonical "matches nothing" key, whatever was authored.
		if (OutPredicate.bUnsatisfiable)
		{
			OutPredicate.RequireAllWords.Reset();
			OutPredicate.RequireAnyWords.Reset();
			OutPredicate.ExcludeWords.Reset();
			OutPredicate.ActiveClauses = 0;
		}
	}

	TSharedPtr<PCGExAssetCollection::FCategory> BuildDerivedPool(
		const PCGExAssetCollection::FCategory& InBase,
		const FTagIndex& InIndex,
		const FCompiledTagPredicate& InPredicate)
	{
		TSharedPtr<PCGExAssetCollection::FCategory> Pool = MakeShared<PCGExAssetCollection::FCategory>(InBase.Name);

		const int32 Num = InBase.Entries.Num();
		Pool->Reserve(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Raw = InBase.Indices[i];
			if (InPredicate.Test(InIndex.GetWords(Raw), InIndex.Stride))
			{
				Pool->RegisterEntry(Raw, InBase.Entries[i]);
			}
		}

		Pool->Compile();
		return Pool;
	}

#pragma region FTagPoolStore

	TSharedPtr<const FTagIndex> FTagPoolStore::GetOrBuildIndex(const TSharedPtr<PCGExAssetCollection::FCategory>& InPool, const uint8 InTagSources)
	{
		if (!InPool)
		{
			return nullptr;
		}

		const TPair<const PCGExAssetCollection::FCategory*, uint8> Key{InPool.Get(), InTagSources};

		FScopeLock Lock(&Mutex);
		if (const TSharedPtr<const FTagIndex>* Found = Indices.Find(Key))
		{
			return *Found;
		}

		TSharedPtr<FTagIndex> Index = MakeShared<FTagIndex>();
		Index->Build(*InPool, InTagSources);
		Indices.Add(Key, Index);
		return Index;
	}

	TSharedPtr<PCGExAssetCollection::FCategory> FTagPoolStore::GetOrBuildPool(
		const TSharedPtr<PCGExAssetCollection::FCategory>& InBase,
		const TSharedPtr<const FTagIndex>& InIndex,
		const FCompiledTagPredicate& InPredicate)
	{
		if (!InBase || !InIndex)
		{
			return nullptr;
		}

		if (InPredicate.IsEmpty())
		{
			return InBase;
		}

		FPoolKey Key;
		Key.Base = InBase.Get();
		Key.Index = InIndex.Get();
		Key.Predicate = InPredicate;

		FScopeLock Lock(&Mutex);
		if (const TSharedPtr<PCGExAssetCollection::FCategory>* Found = Pools.Find(Key))
		{
			return *Found;
		}

		TSharedPtr<PCGExAssetCollection::FCategory> Pool = BuildDerivedPool(*InBase, *InIndex, InPredicate);
		Pools.Add(MoveTemp(Key), Pool);
		return Pool;
	}

	int32 FTagPoolStore::NumPools() const
	{
		FScopeLock Lock(&Mutex);
		return Pools.Num();
	}

#pragma endregion
}
