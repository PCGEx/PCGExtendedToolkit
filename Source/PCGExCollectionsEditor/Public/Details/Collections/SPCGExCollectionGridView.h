// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "SPCGExCollectionGridTile.h"

struct FAssetData;
class FAssetThumbnailPool;
class IStructureDetailsView;
class UPCGExAssetCollection;
class FStructOnScope;
class FTransactionObjectEvent;
class SBorder;
class SScrollBox;
class STextBlock;
class SPCGExCollectionCategoryGroup;

/** Flags describing what kind of structural change happened, so StructuralRefresh() can do the minimum work. */
enum class EPCGExStructuralRefreshFlags : uint8
{
	None           = 0,
	ClearSelection = 1 << 0, // Reset selection state
	ScrollToEnd    = 1 << 1, // Scroll to bottom after refresh
};
ENUM_CLASS_FLAGS(EPCGExStructuralRefreshFlags);

/**
 * Grid/tile view of collection entries with categorized grouping.
 * Left pane: SScrollBox with collapsible category groups, each containing a SWrapBox of tiles.
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
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent) override;

	/** Rebuild the category cache and grouped layout (e.g., after entries are added/removed) */
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

	// Category cache
	TArray<FName> SortedCategoryNames;
	TMap<FName, TArray<int32>> CategoryToEntryIndices;
	TSharedPtr<TArray<TSharedPtr<FName>>> CategoryComboOptions;
	TArray<int32> VisualOrder; // flattened display order of indices

	// Selection
	TSet<int32> SelectedIndices;
	int32 LastClickedIndex = INDEX_NONE;

	// Layout
	TSharedPtr<SScrollBox> GroupScrollBox;
	TMap<FName, TSharedPtr<SPCGExCollectionCategoryGroup>> CategoryGroupWidgets;
	TMap<int32, TSharedPtr<SPCGExCollectionGridTile>> ActiveTiles;

	// Thumbnail cache (persists across rebuilds to prevent flash)
	FThumbnailCacheMap ThumbnailCache;

	// Pinned category header overlay
	TSharedPtr<SBorder> PinnedCategoryHeader;
	TSharedPtr<STextBlock> PinnedHeaderText;
	FName PinnedCategoryName;

	// Collapse state (persists across rebuilds)
	TSet<FName> CollapsedCategories;

	// Detail panel — IStructureDetailsView for editing a single entry struct
	TSharedPtr<IStructureDetailsView> StructDetailView;
	TSharedPtr<FStructOnScope> CurrentStructScope;
	int32 CurrentDetailIndex = INDEX_NONE;

	// Category cache rebuild
	void RebuildCategoryCache();

	// Grouped layout
	void RebuildGroupedLayout();

	// Selection
	void SelectIndex(int32 Index, bool bCtrl, bool bShift);
	void ClearSelection();
	bool IsSelected(int32 Index) const;
	void NotifySelectionChanged();
	void ApplySelectionVisuals();

	// Category operations
	void OnTileDropOnCategory(FName TargetCategory, const TArray<int32>& Indices);
	void OnAssetDropOnCategory(FName TargetCategory, const TArray<FAssetData>& Assets);
	void OnCategoryRenamed(FName OldName, FName NewName);
	void OnAddToCategory(FName Category);
	void OnCategoryExpansionChanged(FName Category, bool bIsExpanded);
	void OnTileReorderInCategory(FName Category, const TArray<int32>& DraggedIndices, int32 InsertBeforeLocalIndex);

	// Lazy tile creation for a single category
	void PopulateCategoryTiles(FName Category);

	// Tile callbacks
	void OnTileClicked(int32 Index, const FPointerEvent& MouseEvent);
	FReply OnTileDragDetected(int32 Index, const FPointerEvent& MouseEvent);
	void OnTileCategoryChanged();

	// Detail panel management
	void UpdateDetailForSelection();
	void SyncStructToCollection(const FProperty* ChangedMemberProperty);
	void OnDetailPropertyChanged(const FPropertyChangedEvent& Event);
	bool bIsSyncing = false;
	bool bIsBatchOperation = false;
	bool bPendingCategoryRefresh = false;

	// Entry struct reflection helpers
	UScriptStruct* GetEntryScriptStruct() const;
	uint8* GetEntryRawPtr(int32 Index) const;

	// Encapsulates reflection boilerplate for Entries array access
	struct FEntriesArrayAccess
	{
		FArrayProperty* ArrayProp = nullptr;
		FStructProperty* InnerProp = nullptr;
		void* ArrayData = nullptr;
		bool IsValid() const { return ArrayProp && ArrayData; }
	};
	FEntriesArrayAccess GetEntriesAccess() const;

	// Incremental layout refresh (tile reuse, no flash)
	void IncrementalCategoryRefresh();

	// Consolidated post-structural-change refresh (all add/dup/delete/undo ops go through here)
	void StructuralRefresh(EPCGExStructuralRefreshFlags Flags = EPCGExStructuralRefreshFlags::None);

	// Scroll tracking for pinned header
	void OnScrolled(float ScrollOffset);

	// Entry operations
	FReply OnAddEntry();
	FReply OnDuplicateSelected();
	FReply OnDeleteSelected();

	// Undo/redo support
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);

	// External modification detection (toolbar buttons, etc.)
	// Deferred to next tick because Modify() fires BEFORE changes are applied.
	void OnObjectModified(UObject* Object);
	bool bPendingExternalRefresh = false;
};
