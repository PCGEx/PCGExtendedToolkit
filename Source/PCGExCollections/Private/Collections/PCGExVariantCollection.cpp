// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExVariantCollection.h"

#include "PCGExLog.h"
#include "Algo/BinarySearch.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "UObject/ObjectSaveContext.h"

PCGEX_REGISTER_COLLECTION_TYPE(Variant, UPCGExVariantCollection, FPCGExAssetCollectionEntry, "Variant Collection", Base)

namespace PCGExVariantCollection
{
	// Order-sensitive digest of the source's EntryIds in raw order. Catches same-count
	// reorders that the entry-count stamp alone would miss.
	inline uint32 ComputeSourceOrderHash(const UPCGExAssetCollection* InSource)
	{
		uint32 Hash = 0;
		InSource->ForEachEntry([&Hash](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Entry->EntryId));
		});
		return Hash;
	}
}

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

void UPCGExVariantCollection::SyncVariantMappings()
{
	int32 FlatOffset = 0;

	// Path rules resolve here, against the declared sources only -- pure authoring shorthand
	// over the explicit rows: the baked output has the same shape. Rule payloads occupy the
	// flat raw-index view after all source-group rows.
	int32 PathPayloadBase = 0;
	for (const FPCGExVariantSource& Group : Sources)
	{
		PathPayloadBase += Group.Overrides.Num();
	}

	// Pick hashes carry raw entry indices in 16 bits by design (see PCGExCollections::PickHash).
	// A flattened view beyond that ceiling would truncate silently -- near impossible in real
	// projects, so just say it loudly if it ever happens.
	if (PathPayloadBase + PathOverrides.Num() > MAX_uint16)
	{
		UE_LOG(LogPCGEx, Error, TEXT("[%s] Variant flat entry count (%d) exceeds the 16-bit pick-index ceiling (%d) -- picks swapped to entries beyond it will resolve to the WRONG entry."),
		       *GetName(), PathPayloadBase + PathOverrides.Num(), MAX_uint16);
	}

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
			UE_LOG(LogPCGEx, Warning, TEXT("[%s] Duplicate asset swap rule for '%s' -- first rule wins."),
			       *GetName(), *Rule.MatchAsset.ToString());
			continue;
		}
		PathToRule.Add(Rule.MatchAsset, r);
	}

	for (FPCGExVariantSource& Group : Sources)
	{
		const int32 GroupCount = Group.Overrides.Num();

		Group.BakedPairs.Reset();
		Group.SourceNumEntriesAtBake = -1;
		Group.SourceOrderHashAtBake = 0;
		Group.SourceGUIDAtBake = 0;

		if (Group.Source.IsNull())
		{
			FlatOffset += GroupCount;
			continue;
		}

		PCGExHelpers::LoadBlocking_AnyThreadTpl(Group.Source);
		const UPCGExAssetCollection* Src = Group.Source.Get();

		if (!Src)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[%s] Variant source '%s' could not be loaded -- group mapping not baked."),
			       *GetName(), *Group.Source.ToSoftObjectPath().ToString());
			FlatOffset += GroupCount;
			continue;
		}

		// EntryId -> raw index lookup over the live source
		TMap<int32, int32> IdToRawIndex;
		IdToRawIndex.Reserve(Src->NumEntries());
		Src->ForEachEntry([&IdToRawIndex](const FPCGExAssetCollectionEntry* Entry, const int32 RawIndex)
		{
			if (Entry->EntryId != 0)
			{
				IdToRawIndex.Add(Entry->EntryId, RawIndex);
			}
		});

		if (IdToRawIndex.IsEmpty() && Src->NumEntries() > 0)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("[%s] Variant source '%s' has no EntryIds -- it was never staging-rebuilt since EntryId support landed. Rebuild & save the source, then re-save this variant."),
			       *GetName(), *Src->GetName());
		}

		for (int32 o = 0; o < GroupCount; o++)
		{
			const FPCGExVariantEntryOverride& Row = Group.Overrides[o];
			if (Row.SourceEntryId == 0 || !Row.Entry.IsValid())
			{
				continue;
			}

			if (const int32* SourceRawIndex = IdToRawIndex.Find(Row.SourceEntryId))
			{
				Group.BakedPairs.Emplace(*SourceRawIndex, FlatOffset + o);
			}
			else
			{
				UE_LOG(LogPCGEx, Warning, TEXT("[%s] Variant row %d for source '%s' references EntryId %d which no longer exists -- orphaned row skipped."),
				       *GetName(), o, *Src->GetName(), Row.SourceEntryId);
			}
		}

		// Path rules fill entries not claimed by an explicit row (specific beats general).
		if (!PathToRule.IsEmpty())
		{
			TSet<int32> ClaimedRawIndices;
			ClaimedRawIndices.Reserve(Group.BakedPairs.Num());
			for (const FIntPoint& Pair : Group.BakedPairs)
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
					Group.BakedPairs.Emplace(RawIndex, PathPayloadBase + *Rule);
				}
			});
		}

		Group.SourceNumEntriesAtBake = Src->NumEntries();
		Group.SourceOrderHashAtBake = PCGExVariantCollection::ComputeSourceOrderHash(Src);
		Group.SourceGUIDAtBake = Src->GetCollectionGUID();

		FlatOffset += GroupCount;
	}

	// Sync is a guaranteed pre-consumption checkpoint (PreSave/cook + editor Sync button) --
	// refresh the flat-view cache here too, covering programmatic mutations that bypassed
	// the PostEditChange notification path.
	RebuildFlatView();
}

bool UPCGExVariantCollection::IsMappingStale(const FPCGExVariantSource& InSourceGroup) const
{
	const UPCGExAssetCollection* Src = InSourceGroup.Source.Get();
	if (!Src)
	{
		// Unloaded source -- cannot verify, report not-stale rather than blocking consumers.
		return false;
	}

	return Src->NumEntries() != InSourceGroup.SourceNumEntriesAtBake
		|| PCGExVariantCollection::ComputeSourceOrderHash(Src) != InSourceGroup.SourceOrderHashAtBake
		|| Src->GetCollectionGUID() != InSourceGroup.SourceGUIDAtBake;
}

const FPCGExVariantSource* UPCGExVariantCollection::FindSourceGroup(const FSoftObjectPath& InSourcePath) const
{
	for (const FPCGExVariantSource& Group : Sources)
	{
		if (Group.Source.ToSoftObjectPath() == InSourcePath)
		{
			return &Group;
		}
	}
	return nullptr;
}

void UPCGExVariantCollection::PreSave(FObjectPreSaveContext SaveContext)
{
	SyncVariantMappings();
	Super::PreSave(SaveContext);
}

void UPCGExVariantCollection::PostLoad()
{
	Super::PostLoad();
	RebuildFlatView();
}

#if WITH_EDITOR
void UPCGExVariantCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		// Value drags can't change the mapping shape.
		RebuildFlatView();
		Super::PostEditChangeProperty(PropertyChangedEvent);
		return;
	}

	// Bake BEFORE Super: its notification chain re-triggers PCG components tracking this
	// asset, and those regenerations read BakedPairs. Ends with RebuildFlatView.
	// KNOWN COST: every non-interactive edit block-loads each source group and walks its
	// entries twice (order hash + id map), on top of the staging rebuild Super triggers.
	// If "editor stutters while editing variants" ever comes up, look here first.
	SyncVariantMappings();
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
