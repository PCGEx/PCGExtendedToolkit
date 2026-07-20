// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "Details/Collections/PCGExAssetCollectionEditor.h"
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
class FPCGExCollectionTileDragDropOp;
class FScriptArrayHelper;

/** Flags describing what kind of structural change happened, so StructuralRefresh() can do the minimum work. */
enum class EPCGExStructuralRefreshFlags : uint8
{
	None           = 0,
	ClearSelection = 1 << 0,
	// Reset selection state
	ScrollToEnd = 1 << 1,
	// Scroll to bottom after refresh
};

ENUM_CLASS_FLAGS(EPCGExStructuralRefreshFlags);

/**
 * Grid/tile view of collection entries with categorized grouping.
 * Left pane: SScrollBox with collapsible category groups, each containing a SWrapBox of tiles.
 * Right pane: IStructureDetailsView showing only the selected entry struct.
 *
 * This widget is created automatically by FPCGExAssetCollectionEditor::CreateGridTab().
 * Custom collection editors normally don't need to subclass this directly -- override the
 * editor's tile picker virtuals instead (GetTilePickerPropertyName, BuildTilePickerWidget).
 */
class PCGEXCOLLECTIONSEDITOR_API SPCGExCollectionGridView : public SCompoundWidget
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
		SLATE_ARGUMENT(TArray<PCGExAssetCollectionEditor::FPushOption>, PushOptions)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override
	{
		return true;
	}

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
	TArray<PCGExAssetCollectionEditor::FPushOption> PushOptions;

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

	// Detail panel -- IStructureDetailsView for editing a single entry struct
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
	void OnTileDropOnCategory(FName TargetCategory, TSharedRef<FPCGExCollectionTileDragDropOp> DragOp, int32 InsertBeforeLocalIndex);
	void OnAssetDropOnCategory(FName TargetCategory, const TArray<FAssetData>& Assets);
	void OnCategoryRenamed(FName OldName, FName NewName);
	void OnAddToCategory(FName Category);
	void OnCategoryExpansionChanged(FName Category, bool bIsExpanded);
	void OnTileReorderInCategory(FName Category, TSharedRef<FPCGExCollectionTileDragDropOp> DragOp, int32 InsertBeforeLocalIndex);

	/**
	 * Cross-collection drop: move-or-copy entries into this collection at PAYLOAD level.
	 * Per-row compatibility is arbitrated by EDITOR_AddEntry; incompatible rows skip with a
	 * notification. When !bIsCopy, only transferred originals leave the source. Single
	 * transaction for both collections.
	 */
	void HandleCrossCollectionDrop(
		const UPCGExAssetCollection* SourceColl,
		const TArray<int32>& SourceIndices,
		FName TargetCategory,
		int32 InsertBeforeLocalIndex,
		bool bIsCopy);

	// Lazy tile creation for a single category
	void PopulateCategoryTiles(FName Category);

	// Tile callbacks
	void OnTileClicked(int32 Index, const FPointerEvent& MouseEvent);
	FReply OnTileDragDetected(int32 Index, const FPointerEvent& MouseEvent);

	void OnTileEntryChanged(int32 SourceIndex, FName PropertyName);

	/** Push the named property from SourceIndex to other selected entries. Struct members
	 *  are pushed via PushStructGated (honors per-element bEnabled gates). No-op outside
	 *  multi-selection or when SourceIndex isn't part of it. */
	void PropagateTileProperty(int32 SourceIndex, FName PropertyName);

	// Detail panel management
	void UpdateDetailForSelection();
	void SyncStructToCollection(const FProperty* ChangedMemberProperty, const FProperty* ChangedLeafProperty);
	void OnDetailPropertyChanged(const FPropertyChangedEvent& Event);

	/**
	 * Recursively propagate only changed sub-properties from NewData to DstData,
	 * using OldData as baseline for comparison.
	 * For array elements with a bEnabled field, only propagates non-gate fields
	 * to elements where bEnabled is true in the destination.
	 */
	static void PropagateChangedProperties(
		const uint8* OldData, const uint8* NewData, uint8* DstData,
		const UStruct* Struct, bool bCheckEnabledGate = false);

	bool bIsSyncing = false;
	bool bIsBatchOperation = false;
	bool bPendingCategoryRefresh = false;

	// Per-row payload access via collection virtuals (payload struct + payload base pointer).
	// Element-level array ops (add/duplicate/reorder) keep using GetEntriesAccess -- element
	// and payload struct differ on wrapper-row hosts.
	UScriptStruct* GetEntryScriptStruct(int32 Index) const;
	uint8* GetEntryRawPtr(int32 Index) const;

	/** Destination-side property for cross-entry copies: same struct reuses SrcProp;
	 *  different structs match by name + SameType. Null when incompatible. */
	static const FProperty* ResolveMatchingProperty(const FProperty* SrcProp, const UScriptStruct* SrcStruct, const UScriptStruct* DstStruct);

	/** Single entry point for every add affordance: untyped direct add on homogeneous
	 *  collections, payload-type dropdown on heterogeneous hosts. */
	void RequestAddEntry(FName Category);

	/** Shared add-entry body (transaction, category, selection, refresh). EntryStruct null = untyped. */
	void AddEntryOfStruct(const UScriptStruct* EntryStruct, FName Category);

	// Encapsulates reflection boilerplate for Entries array access
	struct FEntriesArrayAccess
	{
		FArrayProperty* ArrayProp = nullptr;
		FStructProperty* InnerProp = nullptr;
		void* ArrayData = nullptr;

		bool IsValid() const
		{
			return ArrayProp && ArrayData;
		}
	};

	/** Reflection access to a collection's Entries array. Defaults to the editor's own
	 *  Collection when InColl is null; pass an explicit source for cross-collection ops. */
	FEntriesArrayAccess GetEntriesAccess(const UPCGExAssetCollection* InColl = nullptr) const;

	/**
	 * Pure: computes the desired permutation of CatIndices so the subset matching DraggedSet
	 * lands contiguous at InsertBeforeLocalIndex. Returns empty when no dragged entry belongs
	 * to CatIndices or the resulting order matches CatIndices (i.e. no reordering needed).
	 * When bAdjustForDraggedBefore is true, InsertBefore is treated as a pre-removal index.
	 */
	static TArray<int32> ComputeCategoryDesiredOrder(
		const TArray<int32>& CatIndices,
		const TSet<int32>& DraggedSet,
		int32 InsertBeforeLocalIndex,
		bool bAdjustForDraggedBefore);

	/**
	 * Applies a precomputed DesiredOrder via in-place swaps through Helper. Returns the final
	 * raw entry indices of the dragged values after permutation. Position-map lookup keeps the
	 * permutation O(N).
	 */
	static TArray<int32> ApplyCategoryPermutation(
		FScriptArrayHelper& Helper,
		UScriptStruct* Struct,
		const TArray<int32>& CatIndices,
		const TArray<int32>& DesiredOrder,
		const TSet<int32>& DraggedSet);

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

	/**
	 * Push the listed entry properties from the currently displayed (active) entry
	 * to every other entry in the multi-selection.
	 *
	 * When Option.bRespectEnabledGate is true, the push descends into the property and,
	 * for arrays of structs whose inner type exposes a bEnabled field, skips elements
	 * where the target has bEnabled == true. All other fields are unconditionally
	 * overwritten with the source value.
	 */
	void ExecutePush(const PCGExAssetCollectionEditor::FPushOption& Option);

	/**
	 * Walk a UStruct, copying from Src to Dst.
	 * Arrays of structs whose inner type has a bEnabled boolean are gated per-element:
	 * elements where Dst[i].bEnabled is true are left untouched.
	 * All other properties are unconditionally overwritten.
	 */
	static void PushStructGated(const UStruct* Struct, const uint8* Src, uint8* Dst);

	// Undo/redo support
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);

	// External modification detection (toolbar buttons, etc.)
	// Deferred to next tick because Modify() fires BEFORE changes are applied.
	void OnObjectModified(UObject* Object);
	bool bPendingExternalRefresh = false;
};
