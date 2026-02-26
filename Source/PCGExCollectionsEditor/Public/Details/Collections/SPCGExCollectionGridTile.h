// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IPropertyHandle;
class SBox;
class UPCGExAssetCollection;

DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FOnGetTilePickerWidget, TSharedRef<IPropertyHandle> /*EntryHandle*/);

/**
 * Individual tile widget for the collection grid view.
 * Shows: SubCollection checkbox + Weight spinner (top bar),
 *        Asset thumbnail, Asset picker, Category dropdown.
 */
class SPCGExCollectionGridTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExCollectionGridTile)
		: _TileSize(128.f)
		, _EntryIndex(INDEX_NONE)
	{
	}

	SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, EntryHandle)
	SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
	SLATE_ARGUMENT(FOnGetTilePickerWidget, OnGetPickerWidget)
	SLATE_ARGUMENT(float, TileSize)
	SLATE_ARGUMENT(TWeakObjectPtr<UPCGExAssetCollection>, Collection)
	SLATE_ARGUMENT(int32, EntryIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh the thumbnail (e.g., when the entry's asset changes) */
	void RefreshThumbnail();

	/** Get the property handle for this tile's entry */
	TSharedPtr<IPropertyHandle> GetEntryHandle() const { return EntryHandle; }

private:
	TSharedPtr<IPropertyHandle> EntryHandle;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<FAssetThumbnail> Thumbnail;
	TSharedPtr<SBox> ThumbnailBox;
	TWeakObjectPtr<UPCGExAssetCollection> Collection;
	int32 EntryIndex = INDEX_NONE;
	float TileSize = 128.f;

	// Cached state for short-circuiting RefreshThumbnail when nothing visual changed
	FSoftObjectPath CachedStagingPath;
	bool bCachedIsSubCollection = false;

	/** Build the thumbnail widget from the entry's Staging.Path */
	TSharedRef<SWidget> BuildThumbnailWidget();
};
