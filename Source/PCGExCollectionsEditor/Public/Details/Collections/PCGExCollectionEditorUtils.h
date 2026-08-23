// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class UPCGExAssetCollection;
class UPCGExVariantCollection;
class UPackage;

class UScriptStruct;
struct FPCGExAssetCollectionEntry;

/** Utility functions for collection editing. Operate on any UPCGExAssetCollection. */
namespace PCGExCollectionEditorUtils
{
	/**
	 * Resolve an entry's thumbnail/source path to FAssetData without loading. Falls back to stripping
	 * a trailing "_C" so actor entries (generated class path) resolve to their Blueprint asset.
	 */
	PCGEXCOLLECTIONSEDITOR_API FAssetData ResolveEntryAssetData(const FSoftObjectPath& AssetPath);

	/** UI label for an entry struct: the ShortName meta ("Actor") with display-name fallback. */
	PCGEXCOLLECTIONSEDITOR_API FText GetEntryTypeLabel(const UScriptStruct* EntryStruct);

	/**
	 * Every registered concrete entry type (base entry struct excluded), sorted by label.
	 * The registry-wide sibling of UPCGExAssetCollection::EDITOR_GetAddableEntryTypes --
	 * use the collection virtual when the list must reflect a specific HOST's policy, and
	 * this when any concrete entry type is acceptable (e.g. variant swap payloads).
	 */
	PCGEXCOLLECTIONSEDITOR_API void GetAllConcreteEntryTypes(TArray<const UScriptStruct*>& OutTypes);

	/**
	 * First source entry of the variant staging the given asset (Staging.Path match, source
	 * declaration order, subcollections skipped), with its resolved concrete struct and host.
	 * Null when no LOADED source stages the asset or the entry's type is unregistered.
	 */
	PCGEXCOLLECTIONSEDITOR_API const FPCGExAssetCollectionEntry* FindRuleSeedEntry(
		const UPCGExVariantCollection* InVariant, const FSoftObjectPath& InAssetPath,
		const UScriptStruct*& OutEntryStruct, const UPCGExAssetCollection*& OutHost);

	/** Add Content Browser selection to this collection. */
	PCGEXCOLLECTIONSEDITOR_API void AddBrowserSelection(UPCGExAssetCollection* InCollection);

	/**
	 * Mint EntryIds for every entry still at 0 (never staging-rebuilt since ids landed). Dirties the
	 * collection and, when bNotify, raises a toast naming it -- the ids only persist once it is saved.
	 * No-op during cook. Returns true when anything was minted.
	 */
	PCGEXCOLLECTIONSEDITOR_API bool EnsureEntryIds(UPCGExAssetCollection* InCollection, bool bNotify);

#pragma region Tools

	/** Sort collection by weights in ascending order. */
	PCGEXCOLLECTIONSEDITOR_API void SortByWeightAscending(UPCGExAssetCollection* InCollection);

	/** Sort collection by weights in descending order. */
	PCGEXCOLLECTIONSEDITOR_API void SortByWeightDescending(UPCGExAssetCollection* InCollection);

	/** Set weights to match entry index order. */
	PCGEXCOLLECTIONSEDITOR_API void SetWeightIndex(UPCGExAssetCollection* InCollection);

	/** Add 1 to all weights so it's easier to weight down some assets */
	PCGEXCOLLECTIONSEDITOR_API void PadWeight(UPCGExAssetCollection* InCollection);

	/** Multiplies all weights by 2 */
	PCGEXCOLLECTIONSEDITOR_API void MultWeight(UPCGExAssetCollection* InCollection, int32 Mult);

	/** Reset all weights to 100 */
	PCGEXCOLLECTIONSEDITOR_API void WeightOne(UPCGExAssetCollection* InCollection);

	/** Assign random weights to items */
	PCGEXCOLLECTIONSEDITOR_API void WeightRandom(UPCGExAssetCollection* InCollection);

	/** Normalize weight sum to 100 */
	PCGEXCOLLECTIONSEDITOR_API void NormalizedWeightToSum(UPCGExAssetCollection* InCollection);

#pragma endregion
}
