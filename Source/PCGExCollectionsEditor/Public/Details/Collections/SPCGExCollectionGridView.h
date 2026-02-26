// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STileView.h"

#include "SPCGExCollectionGridTile.h"

class FAssetThumbnailPool;
class IStructureDetailsView;
class IPropertyHandle;
class IPropertyRowGenerator;
class IDetailTreeNode;
class UPCGExAssetCollection;
class FStructOnScope;
class FTransactionObjectEvent;

/**
 * Grid/tile view of collection entries.
 * Left pane: STileView with thumbnails and compact controls per entry.
 * Right pane: IStructureDetailsView showing only the selected entry struct.
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
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Rebuild the tile list (e.g., after entries are added/removed) */
	void RefreshGrid();

	/** Force the detail panel to refresh (e.g., after filter toggle or tile control change) */
	void RefreshDetailPanel();

	/** Get currently selected indices */
	TArray<int32> GetSelectedIndices() const;

private:
	TWeakObjectPtr<UPCGExAssetCollection> Collection;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FOnGetTilePickerWidget OnGetPickerWidget;
	float TileSize = 128.f;

	// Tile view
	TArray<TSharedPtr<int32>> EntryItems;
	TSharedPtr<STileView<TSharedPtr<int32>>> TileView;

	// Detail panel — IStructureDetailsView for editing a single entry struct
	TSharedPtr<IStructureDetailsView> StructDetailView;
	TSharedPtr<FStructOnScope> CurrentStructScope;
	int32 CurrentDetailIndex = INDEX_NONE;

	// Entry operations (via property handle array from row generator)
	TSharedPtr<IPropertyRowGenerator> RowGenerator;
	TSharedPtr<IPropertyHandle> EntriesArrayHandle;

	void RebuildEntryItems();

	// STileView callbacks
	TSharedRef<ITableRow> OnGenerateTile(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);

	// Detail panel management
	void UpdateDetailForSelection();
	void SyncStructToCollection(const FProperty* ChangedMemberProperty);
	void OnDetailPropertyChanged(const FPropertyChangedEvent& Event);
	void OnRowGeneratorPropertyChanged(const FPropertyChangedEvent& Event);
	bool bIsSyncing = false;

	// Entry struct reflection helpers
	UScriptStruct* GetEntryScriptStruct() const;
	uint8* GetEntryRawPtr(int32 Index) const;

	// Entry operations
	FReply OnAddEntry();
	FReply OnDuplicateSelected();
	FReply OnDeleteSelected();

	// Undo/redo support
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);

	// Property row generator setup (for entry operations only)
	void InitRowGenerator();
};
