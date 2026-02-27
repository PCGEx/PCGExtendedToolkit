// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class SBox;
class SBorder;
class UPCGExAssetCollection;
class SWidgetSwitcher;
template <typename OptionType> class SComboBox;

using FThumbnailCacheMap = TMap<FSoftObjectPath, TSharedPtr<FAssetThumbnail>>;

DECLARE_DELEGATE_RetVal_ThreeParams(TSharedRef<SWidget>, FOnGetTilePickerWidget,
	TWeakObjectPtr<UPCGExAssetCollection> /*Collection*/,
	int32 /*EntryIndex*/,
	FSimpleDelegate /*OnAssetChanged*/);
DECLARE_DELEGATE_TwoParams(FOnTileClicked, int32 /*EntryIndex*/, const FPointerEvent& /*MouseEvent*/);
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnTileDragDetected, int32 /*EntryIndex*/, const FPointerEvent& /*MouseEvent*/);

/**
 * Individual tile widget for the collection grid view.
 * Shows: SubCollection checkbox + Weight spinner (top bar),
 *        Asset thumbnail with [i|j] overlay, Asset picker, Category combobox.
 * Supports selection highlight and drag-source for reordering.
 */
class SPCGExCollectionGridTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExCollectionGridTile)
		: _TileSize(128.f)
		, _EntryIndex(INDEX_NONE)
		, _CategoryIndex(INDEX_NONE)
		, _ThumbnailCachePtr(nullptr)
	{
	}

	SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
	SLATE_ARGUMENT(FOnGetTilePickerWidget, OnGetPickerWidget)
	SLATE_ARGUMENT(float, TileSize)
	SLATE_ARGUMENT(TWeakObjectPtr<UPCGExAssetCollection>, Collection)
	SLATE_ARGUMENT(int32, EntryIndex)
	SLATE_ARGUMENT(int32, CategoryIndex)
	SLATE_ARGUMENT(TSharedPtr<TArray<TSharedPtr<FName>>>, CategoryOptions)
	SLATE_ARGUMENT(FThumbnailCacheMap*, ThumbnailCachePtr)
	SLATE_EVENT(FOnTileClicked, OnTileClicked)
	SLATE_EVENT(FOnTileDragDetected, OnTileDragDetected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh the thumbnail (e.g., when the entry's asset changes) */
	void RefreshThumbnail();

	/** Set selection state (visual highlight) */
	void SetSelected(bool bInSelected) { bIsSelected = bInSelected; }

	/** Query selection state */
	bool IsSelected() const { return bIsSelected; }

	// Mouse handling for selection and drag
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	TSharedPtr<FAssetThumbnail> Thumbnail;
	TSharedPtr<SBox> ThumbnailBox;
	TWeakObjectPtr<UPCGExAssetCollection> Collection;
	int32 EntryIndex = INDEX_NONE;
	int32 CategoryIndex = INDEX_NONE;
	float TileSize = 128.f;
	bool bIsSelected = false;
	bool bPendingClick = false;

	// Thumbnail cache (shared across tiles, owned by grid view)
	FThumbnailCacheMap* ThumbnailCachePtr = nullptr;

	// Category combobox
	TSharedPtr<TArray<TSharedPtr<FName>>> CategoryOptions;
	TSharedPtr<SWidgetSwitcher> CategoryWidgetSwitcher;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> CategoryCombo;

	// Delegates
	FOnTileClicked OnTileClicked;
	FOnTileDragDetected OnTileDragDetected;

	// Cached state for short-circuiting RefreshThumbnail when nothing visual changed
	FSoftObjectPath CachedStagingPath;
	bool bCachedIsSubCollection = false;

	/** Build the thumbnail widget from the entry's Staging.Path */
	TSharedRef<SWidget> BuildThumbnailWidget();
};
