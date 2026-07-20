// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "AssetRegistry/AssetData.h"

class UPCGExAssetCollection;

/**
 * Generic implementations of the per-collection-type editor actions. The concrete actions
 * (registered via PCGEX_REGISTER_COLLECTION_EDITOR_TYPE) supply the collection's UClass and
 * default asset-name prefix; everything else is type-agnostic.
 */
namespace PCGExCollectionEditorHelpers
{
	/**
	 * Create a collection asset of CollectionClass (or return the existing one the user
	 * targets via the save dialog). Null on cancel/failure. Caller populates and saves.
	 */
	PCGEXCOLLECTIONSEDITOR_API UPCGExAssetCollection* CreateCollectionAsset(
		const FString& AnchorPackagePath,
		UClass* CollectionClass,
		const TCHAR* DefaultAssetName,
		bool bOpenSaveDialog = true);

	/**
	 * Create a new collection asset of CollectionClass and append the source assets to it.
	 * When bOpenSaveDialog is true, the user picks the destination folder and asset name via a
	 * modal save dialog; otherwise the asset is created as DefaultAssetName in the same package
	 * path as the first selected source asset.
	 */
	PCGEXCOLLECTIONSEDITOR_API void CreateCollectionFromTyped(
		const TArray<FAssetData>& SelectedAssets,
		UClass* CollectionClass,
		const TCHAR* DefaultAssetName,
		bool bOpenSaveDialog = true);

	/**
	 * Merge the selected collection assets into an Omni picked via the save dialog (new or
	 * existing). Copy/transfer semantics live in EDITOR_AppendCollections; sources untouched.
	 */
	PCGEXCOLLECTIONSEDITOR_API void MergeCollectionsIntoOmni(const TArray<FAssetData>& SelectedCollections);

	/** Append the given source assets to each selected collection. */
	PCGEXCOLLECTIONSEDITOR_API void UpdateCollectionsFromTyped(
		const TArray<TObjectPtr<UPCGExAssetCollection>>& SelectedCollections,
		const TArray<FAssetData>& SelectedAssets);
}
