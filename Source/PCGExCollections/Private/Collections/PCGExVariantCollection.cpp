// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExVariantCollection.h"

#include "PCGExLog.h"
#include "Algo/BinarySearch.h"

PCGEX_REGISTER_COLLECTION_TYPE(Variant, UPCGExVariantCollection, FPCGExAssetCollectionEntry, "Variant Collection", Base)

bool UPCGExVariantCollection::IsValidIndex(const int32 InIndex) const
{
	return InIndex >= 0 && InIndex < NumEntries();
}

int32 UPCGExVariantCollection::NumEntries() const
{
	if (FlatTotalEntries >= 0)
	{
		return FlatTotalEntries;
	}

	int32 Total = PathOverrides.Num();
	for (const FPCGExVariantSource& Group : Sources)
	{
		Total += Group.Overrides.Num();
	}
	return Total;
}

void UPCGExVariantCollection::BuildCache()
{
	// Flatten to base pointers in declaration order. Unset rows contribute a null -- tolerated
	// by BuildCacheFromEntryPtrs -- so raw indices stay stable under partial authoring.
	TArray<FPCGExAssetCollectionEntry*> EntryPtrs;
	EntryPtrs.Reserve(NumEntries());

	for (FPCGExVariantSource& Group : Sources)
	{
		for (FPCGExVariantEntryOverride& Row : Group.Overrides)
		{
			EntryPtrs.Add(Row.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>());
		}
	}

	for (FPCGExVariantPathOverride& Rule : PathOverrides)
	{
		EntryPtrs.Add(Rule.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>());
	}

	BuildCacheFromEntryPtrs(EntryPtrs);
}

void UPCGExVariantCollection::ForEachEntry(FForEachConstEntryFunc Iterator) const
{
	int32 FlatIndex = 0;
	for (const FPCGExVariantSource& Group : Sources)
	{
		for (const FPCGExVariantEntryOverride& Row : Group.Overrides)
		{
			// Unset rows are skipped but still consume their flat index -- iterator
			// consumers rely on the index being the raw entry index.
			if (const FPCGExAssetCollectionEntry* Entry = Row.Entry.GetPtr<FPCGExAssetCollectionEntry>())
			{
				Iterator(Entry, FlatIndex);
			}
			FlatIndex++;
		}
	}
	for (const FPCGExVariantPathOverride& Rule : PathOverrides)
	{
		if (const FPCGExAssetCollectionEntry* Entry = Rule.Entry.GetPtr<FPCGExAssetCollectionEntry>())
		{
			Iterator(Entry, FlatIndex);
		}
		FlatIndex++;
	}
}

void UPCGExVariantCollection::ForEachEntry(FForEachEntryFunc Iterator)
{
	int32 FlatIndex = 0;
	for (FPCGExVariantSource& Group : Sources)
	{
		for (FPCGExVariantEntryOverride& Row : Group.Overrides)
		{
			if (FPCGExAssetCollectionEntry* Entry = Row.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>())
			{
				Iterator(Entry, FlatIndex);
			}
			FlatIndex++;
		}
	}
	for (FPCGExVariantPathOverride& Rule : PathOverrides)
	{
		if (FPCGExAssetCollectionEntry* Entry = Rule.Entry.GetMutablePtr<FPCGExAssetCollectionEntry>())
		{
			Iterator(Entry, FlatIndex);
		}
		FlatIndex++;
	}
}

void UPCGExVariantCollection::BuildGroupMapping(const int32 GroupIdx, TArray<FIntPoint>& OutPairs) const
{
	OutPairs.Reset();

	if (!Sources.IsValidIndex(GroupIdx))
	{
		return;
	}

	const FPCGExVariantSource& Group = Sources[GroupIdx];
	const UPCGExAssetCollection* Src = Group.SourceCollection;
	if (!Src)
	{
		return;
	}

	// Flat offset of this group's rows + start of the PathOverrides payload tail. Derived locally --
	// one definition of the flat layout lives in RebuildFlatView; this mirrors it without depending
	// on the cache having been built.
	int32 FlatOffset = 0;
	int32 PathPayloadBase = 0;
	for (int32 g = 0; g < Sources.Num(); g++)
	{
		if (g < GroupIdx)
		{
			FlatOffset += Sources[g].Overrides.Num();
		}
		PathPayloadBase += Sources[g].Overrides.Num();
	}

	// Pick hashes carry raw entry indices in 16 bits by design (see PCGExCollections::PickHash).
	// A flattened view beyond that ceiling would truncate silently -- near impossible in real
	// projects, so just say it loudly if it ever happens. Variant-level diagnostics (this and the
	// duplicate-rule warning) fire on the first group only: consumers call per group, per execution.
	if (GroupIdx == 0 && PathPayloadBase + PathOverrides.Num() > MAX_uint16)
	{
		UE_LOG(LogPCGEx, Error, TEXT("[%s] Variant flat entry count (%d) exceeds the 16-bit pick-index ceiling (%d) -- picks swapped to entries beyond it will resolve to the WRONG entry."),
		       *GetName(), PathPayloadBase + PathOverrides.Num(), MAX_uint16);
	}

	bool bAnyResolved = false;
	bool bAnyBound = false;
	for (int32 o = 0; o < Group.Overrides.Num(); o++)
	{
		const FPCGExVariantEntryOverride& Row = Group.Overrides[o];
		if (Row.SourceEntryId == 0 || !Row.Entry.IsValid())
		{
			continue;
		}

		bAnyBound = true;
		const int32 SourceRawIndex = Src->FindRawIndexByEntryId(Row.SourceEntryId);
		if (SourceRawIndex != INDEX_NONE)
		{
			bAnyResolved = true;
			OutPairs.Emplace(SourceRawIndex, FlatOffset + o);
		}
		else
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[%s] Variant row %d for source '%s' references EntryId %d which no longer exists -- orphaned row skipped."),
			       *GetName(), o, *Src->GetName(), Row.SourceEntryId);
		}
	}

	if (bAnyBound && !bAnyResolved && Src->NumEntries() > 0)
	{
		UE_LOG(LogPCGEx, Warning, TEXT("[%s] Variant source '%s' rows resolved nothing -- if the source was never staging-rebuilt since EntryId support landed, rebuild & save it."),
		       *GetName(), *Src->GetName());
	}

	// Asset-path swap rules fill source entries not claimed by an explicit row (specific beats
	// general). First matching rule wins; rule payloads occupy the flat tail after all group rows.
	TMap<FSoftObjectPath, int32> PathToRule;
	PathToRule.Reserve(PathOverrides.Num());
	for (int32 r = 0; r < PathOverrides.Num(); r++)
	{
		const FPCGExVariantPathOverride& Rule = PathOverrides[r];
		if (Rule.MatchAsset.IsNull() || !Rule.Entry.IsValid())
		{
			continue;
		}
		if (PathToRule.Contains(Rule.MatchAsset))
		{
			if (GroupIdx == 0)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[%s] Duplicate asset swap rule for '%s' -- first rule wins."),
				       *GetName(), *Rule.MatchAsset.ToString());
			}
			continue;
		}
		PathToRule.Add(Rule.MatchAsset, r);
	}

	if (!PathToRule.IsEmpty())
	{
		TSet<int32> ClaimedRawIndices;
		ClaimedRawIndices.Reserve(OutPairs.Num());
		for (const FIntPoint& Pair : OutPairs)
		{
			ClaimedRawIndices.Add(Pair.X);
		}

		Src->ForEachEntry([&](const FPCGExAssetCollectionEntry* Entry, const int32 RawIndex)
		{
			if (Entry->bIsSubCollection || ClaimedRawIndices.Contains(RawIndex))
			{
				return;
			}
			if (const int32* Rule = PathToRule.Find(Entry->Staging.Path))
			{
				OutPairs.Emplace(RawIndex, PathPayloadBase + *Rule);
			}
		});
	}
}

const FPCGExVariantSource* UPCGExVariantCollection::FindSourceGroup(const FSoftObjectPath& InSourcePath) const
{
	for (const FPCGExVariantSource& Group : Sources)
	{
		if (FSoftObjectPath(Group.SourceCollection) == InSourcePath)
		{
			return &Group;
		}
	}
	return nullptr;
}

void UPCGExVariantCollection::PostLoad()
{
	Super::PostLoad();

	// Soft -> hard source migration. The sync load is editor/cook-only (both sync-load-tolerant, game
	// thread); cooked packages are re-serialized by the cook, so packaged data is always migrated and
	// the runtime branch only ever heals in-editor-runtime edge cases via resident resolve. The carrier
	// is kept on failure so a later load can retry, and cleared once the hard ref holds.
	for (FPCGExVariantSource& Group : Sources)
	{
		if (!Group.SourceCollection && !Group.Source_DEPRECATED.IsNull())
		{
#if WITH_EDITOR
			Group.SourceCollection = Cast<UPCGExAssetCollection>(Group.Source_DEPRECATED.ToSoftObjectPath().TryLoad());
#else
			Group.SourceCollection = Group.Source_DEPRECATED.Get();
#endif
			if (!Group.SourceCollection)
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[%s] Variant source '%s' could not be resolved during migration -- group inert until it resolves."),
				       *GetName(), *Group.Source_DEPRECATED.ToSoftObjectPath().ToString());
				continue;
			}
		}
		Group.Source_DEPRECATED.Reset();
	}

	RebuildFlatView();
}

#if WITH_EDITOR
void UPCGExVariantCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// No bake step: mappings derive live at consumption. Only the flat view depends on shape.
	RebuildFlatView();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UPCGExVariantCollection::RebuildFlatView()
{
	FlatGroupOffsets.Reset(Sources.Num() + 1);

	int32 Offset = 0;
	for (const FPCGExVariantSource& Group : Sources)
	{
		FlatGroupOffsets.Add(Offset);
		Offset += Group.Overrides.Num();
	}
	FlatGroupOffsets.Add(Offset); // start of the PathOverrides payload tail

	FlatTotalEntries = Offset + PathOverrides.Num();
}

const FPCGExAssetCollectionEntry* UPCGExVariantCollection::GetEntryAtRawIndex(const int32 Index) const
{
	return ResolveRawIndex(Index);
}

FPCGExAssetCollectionEntry* UPCGExVariantCollection::GetMutableEntryAtRawIndex(const int32 Index)
{
	return ResolveRawIndex(Index);
}

FPCGExAssetCollectionEntry* UPCGExVariantCollection::ResolveRawIndex(const int32 Index)
{
	return const_cast<FPCGExAssetCollectionEntry*>(const_cast<const UPCGExVariantCollection*>(this)->ResolveRawIndex(Index));
}

const FPCGExAssetCollectionEntry* UPCGExVariantCollection::ResolveRawIndex(const int32 Index) const
{
	const FInstancedStruct* Payload = ResolveRawPayload(Index);
	return Payload ? Payload->GetPtr<FPCGExAssetCollectionEntry>() : nullptr;
}

const FInstancedStruct* UPCGExVariantCollection::ResolveRawPayload(const int32 Index) const
{
	if (Index < 0)
	{
		return nullptr;
	}

	if (FlatTotalEntries >= 0)
	{
		if (Index >= FlatTotalEntries)
		{
			return nullptr;
		}

		const int32 PathBase = FlatGroupOffsets.Last();
		if (Index >= PathBase)
		{
			return &PathOverrides[Index - PathBase].Entry;
		}

		// Offsets are ascending (empty groups collapse onto the next start); UpperBound lands
		// past every start <= Index, so -1 is the last group actually containing the index.
		const int32 GroupIdx = Algo::UpperBound(FlatGroupOffsets, Index) - 1;
		return &Sources[GroupIdx].Overrides[Index - FlatGroupOffsets[GroupIdx]].Entry;
	}

	// Fallback: flat view not built yet (fresh object before any load/edit notification).
	int32 Offset = 0;
	for (const FPCGExVariantSource& Group : Sources)
	{
		const int32 Count = Group.Overrides.Num();
		if (Index < Offset + Count)
		{
			return &Group.Overrides[Index - Offset].Entry;
		}
		Offset += Count;
	}
	if (PathOverrides.IsValidIndex(Index - Offset))
	{
		return &PathOverrides[Index - Offset].Entry;
	}
	return nullptr;
}

#if WITH_EDITOR
const UScriptStruct* UPCGExVariantCollection::EDITOR_GetEntryScriptStruct(const int32 RawIndex) const
{
	const FInstancedStruct* Payload = ResolveRawPayload(RawIndex);
	return Payload ? Payload->GetScriptStruct() : nullptr;
}
#endif
