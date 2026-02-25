// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STileView.h"

#include "SPCGExCollectionGridTile.h"

class FAssetThumbnailPool;
class IPropertyHandle;
class IPropertyRowGenerator;
class IDetailTreeNode;
class UPCGExAssetCollection;
class UPCGExCollectionsEditorSettings;

/**
 * Grid/tile view of collection entries.
 * Left pane: STileView with thumbnails and compact controls per entry.
 * Right pane: Detail panel for selected entry properties (via IPropertyRowGenerator).
 */
class SPCGExCollectionGridView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExCollectionGridView)
		: _TileSize(128.f)
	{
	}

	SLATE_ARGUMENT(UPCGExAssetCollection*, Collection)
	SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
	SLATE_ARGUMENT(FOnGetTilePickerWidget, OnGetPickerWidget)
	SLATE_ARGUMENT(float, TileSize)
	/** Property names that are already shown on the tile (excluded from detail panel) */
	SLATE_ARGUMENT(TSet<FName>, TilePropertyNames)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Rebuild the tile list (e.g., after entries are added/removed) */
	void RefreshGrid();

	/** Force the detail panel to refresh (e.g., after filter toggle) */
	void RefreshDetailPanel();

	/** Get currently selected indices */
	TArray<int32> GetSelectedIndices() const;

private:
	TWeakObjectPtr<UPCGExAssetCollection> Collection;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnGetTilePickerWidget OnGetPickerWidget;
	float TileSize = 128.f;
	TSet<FName> TilePropertyNames;

	// Tile view
	TArray<TSharedPtr<int32>> EntryItems;
	TSharedPtr<STileView<TSharedPtr<int32>>> TileView;

	// Detail panel
	TSharedPtr<SVerticalBox> DetailPanelBox;
	TSharedPtr<IPropertyRowGenerator> RowGenerator;

	// Entry operations (via property handle array)
	TSharedPtr<IPropertyHandle> EntriesArrayHandle;

	void RebuildEntryItems();

	// STileView callbacks
	TSharedRef<ITableRow> OnGenerateTile(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);

	// Detail panel population
	void PopulateDetailPanel(const TArray<int32>& SelectedIndices);

	// Entry operations
	FReply OnAddEntry();
	FReply OnDuplicateSelected();
	FReply OnDeleteSelected();

	// Property row generator setup
	void InitRowGenerator();
};
