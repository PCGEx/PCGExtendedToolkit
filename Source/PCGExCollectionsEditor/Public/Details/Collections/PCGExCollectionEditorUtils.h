// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class UPCGExAssetCollection;
class UPackage;
struct FAssetData;
struct FSoftObjectPath;

/** Utility functions for collection editing. Operate on any UPCGExAssetCollection. */
namespace PCGExCollectionEditorUtils
{
	/**
	 * Resolve an entry's thumbnail/source path to FAssetData without loading. Falls back to stripping
	 * a trailing "_C" so actor entries (generated class path) resolve to their Blueprint asset.
	 */
	PCGEXCOLLECTIONSEDITOR_API FAssetData ResolveEntryAssetData(const FSoftObjectPath& AssetPath);

	/** Add Content Browser selection to this collection. */
	PCGEXCOLLECTIONSEDITOR_API void AddBrowserSelection(UPCGExAssetCollection* InCollection);

	/**
	 * Create a new collection of InCollectionClass from a content-browser selection, then add that
	 * selection to it. When bOpenSaveDialog is true, the user picks the destination folder and name
	 * through the Save Asset dialog; otherwise the asset is created next to the first selected asset
	 * using InDefaultAssetName. Returns the target collection, or nullptr if the user cancelled the
	 * dialog or creation failed.
	 */
	PCGEXCOLLECTIONSEDITOR_API UPCGExAssetCollection* CreateCollectionFromSelection(
		TSubclassOf<UPCGExAssetCollection> InCollectionClass,
		const FString& InDefaultAssetName,
		const TArray<FAssetData>& InSelectedAssets,
		bool bOpenSaveDialog = true);

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
