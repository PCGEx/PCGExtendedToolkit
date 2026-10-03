// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionTags.h"
#include "Elements/PCGExAssetCollectionToSet.h"

struct FPCGExContext;
class UPCGData;
class UPCGExAssetCollection;
struct FPCGExAssetCollectionEntry;
struct FPCGExNameFiltersDetails;
struct FPCGExGetCollectionDataContext;
class UPCGExGetCollectionDataSettings;

namespace PCGExGetCollectionData
{
	// Pin labels + the empty-output tag. inline so the same FNames are shared across every TU that
	// includes this header -- the values are initialized once at static-init time, then read-only.
	inline const FName SourcesPin = TEXT("Sources");
	inline const FName OutputCollectionDataPin = TEXT("Data");
	inline const FName AnnotatedSourcesPin = TEXT("Annotated Sources");
	inline const FName EmptyTag = TEXT("empty");

	/** Collected entry along with the collection that directly owns it and the resolved category.
	 *  RootCollectionIndex / CollectionIndex / NestingDepth / CollectionHash are stamped during
	 *  flatten so the write loop can emit them without re-deriving identity per row. -1 sentinel
	 *  for "unset" / null-entry rows. NestingDepth is 0 for top-level entries and increments by 1
	 *  per sub-collection recursion. CollectionHash is a dense counter shared across the node
	 *  invocation -- same (Root, Coll, Depth) tuple always maps to the same hash. */
	struct FFlattenedEntry
	{
		const FPCGExAssetCollectionEntry* Entry = nullptr;
		const UPCGExAssetCollection* Host = nullptr;
		FName Category = NAME_None;
		int32 RootCollectionIndex = -1;
		int32 CollectionIndex = -1;
		int32 NestingDepth = -1;
		int32 CollectionHash = -1;
	};

	/** Invariants shared across the flatten recursion. CollectionIndexMap + NextCollectionIndex and
	 *  CollectionHashMap + NextCollectionHash are owned by AdvanceWork and shared across every
	 *  FlattenInto in a node invocation, so per-host / per-tuple indices are globally unique and
	 *  assigned in deterministic discovery order. SeenEntries is per-FlattenInto -- ProcessEntry
	 *  inserts on every emit so the bNoDuplicates check stays O(1) instead of scanning OutEntries. */
	struct FProcessEntryContext
	{
		FPCGExContext* Context = nullptr;
		const FPCGExNameFiltersDetails* CategoryFilters = nullptr;
		/** Parsed tag clauses, null when the tag filter is off. Tested at depth 0, or at every depth when bTagFilterRecursive. */
		const PCGExCollections::Tags::FTagLists* TagFilter = nullptr;
		uint8 TagSources = 0;
		bool bTagFilterRecursive = false;
		EPCGExSubCollectionToSet SubHandling = EPCGExSubCollectionToSet::Ignore;
		EPCGExCategoryInheritance CategoryInheritance = EPCGExCategoryInheritance::None;
		bool bOmitInvalidAndEmpty = true;
		bool bNoDuplicates = true;
		TMap<const UPCGExAssetCollection*, int32>* CollectionIndexMap = nullptr;
		int32* NextCollectionIndex = nullptr;
		TMap<TTuple<int32, int32, int32>, int32>* CollectionHashMap = nullptr;
		int32* NextCollectionHash = nullptr;
		TSet<const FPCGExAssetCollectionEntry*>* SeenEntries = nullptr;
	};

	/** Lookup-or-assign helper: returns the global CollectionIndex for `Host`, allocating a fresh
	 *  one on first encounter. Returns -1 when Host is null or the map/counter isn't wired up. */
	FORCEINLINE int32 GetOrAssignCollectionIndex(const FProcessEntryContext& Ctx, const UPCGExAssetCollection* Host)
	{
		if (!Host || !Ctx.CollectionIndexMap || !Ctx.NextCollectionIndex)
		{
			return -1;
		}
		if (const int32* Existing = Ctx.CollectionIndexMap->Find(Host))
		{
			return *Existing;
		}
		const int32 Assigned = (*Ctx.NextCollectionIndex)++;
		Ctx.CollectionIndexMap->Add(Host, Assigned);
		return Assigned;
	}

	/** Lookup-or-assign helper: returns the dense CollectionHash for the (Root, Coll, Depth) tuple,
	 *  allocating a fresh one on first encounter. Same tuple => same hash, every time. Returns -1
	 *  when the map/counter isn't wired up. */
	FORCEINLINE int32 GetOrAssignCollectionHash(const FProcessEntryContext& Ctx, const int32 RootIdx, const int32 CollIdx, const int32 Depth)
	{
		if (!Ctx.CollectionHashMap || !Ctx.NextCollectionHash)
		{
			return -1;
		}
		const TTuple<int32, int32, int32> Key(RootIdx, CollIdx, Depth);
		if (const int32* Existing = Ctx.CollectionHashMap->Find(Key))
		{
			return *Existing;
		}
		const int32 Assigned = (*Ctx.NextCollectionHash)++;
		Ctx.CollectionHashMap->Add(Key, Assigned);
		return Assigned;
	}

	/** Recursive flatten walker -- recurses into sub-collections per SubHandling, applies category
	 *  filter + inheritance, dedups against pointer-identical entries when bNoDuplicates is set,
	 *  stamps RootCollectionIndex / CollectionIndex / NestingDepth / CollectionHash on every
	 *  emitted entry. */
	void ProcessEntry(
		const FProcessEntryContext& Ctx,
		const FPCGExAssetCollectionEntry* InEntry,
		const UPCGExAssetCollection* InHost,
		TArray<FFlattenedEntry>& OutEntries,
		const FName EffectiveParentCategory,
		const int32 RootCollectionIndex,
		const int32 Depth,
		TSet<uint64>& GUIDS);

	/** Top-level flatten entry point: walks `Collection`'s direct entries, calling ProcessEntry for
	 *  each. Caller owns OutEntries -- safe to call multiple times to append from several roots. */
	void FlattenCollection(
		const FProcessEntryContext& Ctx,
		UPCGExAssetCollection* Collection,
		const int32 RootCollectionIndex,
		TArray<FFlattenedEntry>& OutEntries);

	/** Single-value @Data-domain read for the PerInputData fanout mode. Falls back from
	 *  FSoftObjectPath to FString (authored-as-string paths) before giving up. */
	FSoftObjectPath ReadSinglePath(FPCGExContext* InContext, const UPCGData* InData, const FName AttributeName);

	/** Single-value @Data-domain int64 hash read for EntryIdAndMap mode. */
	int64 ReadSingleHash(FPCGExContext* InContext, const UPCGData* InData, const FName AttributeName);

	/** Build `Context->Slots` + `Context->ResolvedCollections` from `SourcesPin` inputs. Encapsulates
	 *  the three-phase parse (parallel bulk-read, single-threaded slot allocation, optional parallel
	 *  hash->subcollection resolution) plus the final batch async load. Returns false on hard error
	 *  (e.g. EntryIdAndMap mode with no valid Map pin). Empty inputs is success-with-nothing-to-do. */
	bool ParseSourceInputsIntoSlots(
		FPCGExGetCollectionDataContext* Context,
		const UPCGExGetCollectionDataSettings* Settings);
}
