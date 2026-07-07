// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "Details/Collections/SPCGExCollectionGridTile.h" // FThumbnailCacheMap

struct FAssetData;
class FAssetThumbnail;
class FAssetThumbnailPool;
class FStructOnScope;
class FTransactionObjectEvent;
class IStructureDetailsView;
class SBorder;
class SBox;
class SScrollBox;
class SPCGExCollectionCategoryGroup;
class UPCGExAssetCollection;
class UPCGExVariantCollection;

/** Visual/behavioral state of a variant grid tile. */
enum class EPCGExVariantTileState : uint8
{
	PassThrough,    // Source entry with no override — dimmed, offers "declare swap"
	Swapped,        // Explicit override declared — shows replacement, offers revoke
	SwappedByRule,  // Covered by an asset-path rule — shows the rule's replacement, offers "specialize"
	RuleDefinition, // A path rule itself (synthetic "Asset Swaps" group) — offers delete
	Orphaned,       // Override row whose SourceEntryId no longer exists on the source
};

/**
 * One tile of the variant grid. The unit is a SOURCE collection entry (auto-populated),
 * not an authored row: undeclared entries render dimmed pass-through tiles, declared
 * swaps render the replacement thumbnail with the source as a corner badge.
 */
struct FPCGExVariantGridItem
{
	int32 GroupIdx = INDEX_NONE;       // index into Variant->Sources (INDEX_NONE for rule-definition tiles)
	int32 SourceRawIndex = INDEX_NONE; // raw entry index on the source collection (INDEX_NONE for orphans/rules)
	int32 SourceEntryId = 0;           // stable id binding tile to source entry
	int32 OverrideRowIdx = INDEX_NONE; // index into the group's Overrides (INDEX_NONE = no explicit row)
	int32 PathRuleIdx = INDEX_NONE;    // index into Variant->PathOverrides when rule-covered / rule tile
	bool bIsRuleDefinition = false;    // tile in the synthetic "Asset Swaps" group

	FSoftObjectPath SourceThumbPath;
	FSoftObjectPath OverrideThumbPath;
	FText Label;

	EPCGExVariantTileState GetState() const
	{
		if (bIsRuleDefinition) { return EPCGExVariantTileState::RuleDefinition; }
		if (SourceRawIndex == INDEX_NONE) { return EPCGExVariantTileState::Orphaned; }
		if (OverrideRowIdx != INDEX_NONE) { return EPCGExVariantTileState::Swapped; }
		return PathRuleIdx != INDEX_NONE ? EPCGExVariantTileState::SwappedByRule : EPCGExVariantTileState::PassThrough;
	}
};

DECLARE_DELEGATE_OneParam(FOnVariantTileAction, int32 /*ItemIndex*/);

/** Tile widget for SPCGExVariantGridView. Purely display + action affordances; all state lives in the grid. */
class SPCGExVariantGridTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExVariantGridTile)
			: _TileSize(192.f)
			  , _ItemIndex(INDEX_NONE)
			  , _ThumbnailCachePtr(nullptr)
		{
		}

		SLATE_ARGUMENT(float, TileSize)
		SLATE_ARGUMENT(int32, ItemIndex)
		SLATE_ARGUMENT(FPCGExVariantGridItem, Item)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_ARGUMENT(FThumbnailCacheMap*, ThumbnailCachePtr)
		SLATE_EVENT(FOnVariantTileAction, OnTileClicked)
		SLATE_EVENT(FOnVariantTileAction, OnDeclareSwap)
		SLATE_EVENT(FOnVariantTileAction, OnRevokeSwap)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Refresh displayed item data in place — the tile widget itself persists in its parent
	 * wrap box (no teardown, no scroll jump); inner content only rebuilds when display-relevant
	 * fields actually changed.
	 */
	void UpdateItem(const FPCGExVariantGridItem& InItem);

	void SetSelected(const bool bInSelected) { bIsSelected = bInSelected; }
	bool IsSelected() const { return bIsSelected; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	float TileSize = 192.f;
	int32 ItemIndex = INDEX_NONE;
	FPCGExVariantGridItem Item;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FThumbnailCacheMap* ThumbnailCachePtr = nullptr;
	TSharedPtr<SBorder> RootBorder;
	bool bIsSelected = false;

	/** (Re)builds the tile content into RootBorder from the current Item. */
	void RebuildContent();

	FOnVariantTileAction OnTileClicked;
	FOnVariantTileAction OnDeclareSwap;
	FOnVariantTileAction OnRevokeSwap;

	/** Pooled, cache-backed thumbnail widget for an arbitrary asset path (0-size path → placeholder text). */
	TSharedRef<SWidget> MakeThumbnail(const FSoftObjectPath& AssetPath, float InSize) const;
};

/**
 * Grid view for UPCGExVariantCollection: one collapsible group per source collection,
 * one tile per SOURCE entry (auto-populated from the live source), opt-in swap declaration
 * per tile, and a right-pane structure details view editing the replacement entry payload.
 * EntryIds never surface — binding is handled on declare.
 */
class PCGEXCOLLECTIONSEDITOR_API SPCGExVariantGridView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExVariantGridView)
			: _TileSize(192.f)
		{
		}

		SLATE_ARGUMENT(UPCGExVariantCollection*, Collection)
		SLATE_ARGUMENT(TSharedPtr<FAssetThumbnailPool>, ThumbnailPool)
		SLATE_ARGUMENT(float, TileSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SPCGExVariantGridView() override;

	/**
	 * Rebuild the item model from the live sources and refresh the display. Preserves selection
	 * by identity. When the item structure is unchanged (payload edits, declare/revoke), tiles
	 * update in place — no layout teardown, no scroll jump; a full layout rebuild only happens
	 * on structural changes (source or rule added/removed).
	 * @param bRefreshDetailPanel Pass false when the edit originated from the details pane itself.
	 */
	void RefreshGrid(bool bRefreshDetailPanel = true);

	/**
	 * Declare the collection assets among InAssets as new sources. The variant itself and
	 * already-declared sources are skipped (with a toast); other asset types are ignored.
	 * Shared by the toolbar picker and the drag-drop paths.
	 * @return The number of candidate collections handled (declared + skipped duplicates).
	 */
	int32 AddSourcesFromAssets(const TArray<FAssetData>& InAssets);

	//~ Drop target: collection assets dropped anywhere on the grid become sources.
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent) override;

private:
	TWeakObjectPtr<UPCGExVariantCollection> Collection;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	float TileSize = 192.f;

	// Item model (rebuilt by RefreshGrid)
	TArray<FPCGExVariantGridItem> Items;
	TArray<FName> SortedGroupNames;
	TMap<FName, TArray<int32>> GroupToItems; // group display name -> item indices

	// Layout
	TSharedPtr<SScrollBox> GroupScrollBox;
	TMap<FName, TSharedPtr<SPCGExCollectionCategoryGroup>> GroupWidgets;
	TMap<int32, TSharedPtr<SPCGExVariantGridTile>> ActiveTiles;
	TSet<FName> CollapsedGroups;

	FThumbnailCacheMap ThumbnailCache;

	// Selection (single-select v1)
	int32 SelectedItem = INDEX_NONE;

	// Details pane
	TSharedPtr<IStructureDetailsView> StructDetailView;
	TSharedPtr<FStructOnScope> CurrentStructScope;
	TSharedPtr<SBox> DetailsHost;

	bool bIsSyncing = false;

	void RebuildItems();
	void RebuildLayout();
	void PopulateGroupTiles(FName GroupName);

	void OnTileClicked(int32 ItemIndex);
	void DeclareSwap(int32 ItemIndex);
	void RevokeSwap(int32 ItemIndex);

	void ApplySelectionVisuals();
	void UpdateDetailForSelection();
	void OnDetailPropertyChanged(const FPropertyChangedEvent& Event);

	/** Resolve an item's override row payload memory + struct, or false when not declared/invalid. */
	bool ResolveOverridePayload(const FPCGExVariantGridItem& InItem, UScriptStruct*& OutStruct, uint8*& OutMemory) const;

	/** Undo/redo — refresh when our collection is transacted. */
	void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event);
	FDelegateHandle TransactedHandle;
};
