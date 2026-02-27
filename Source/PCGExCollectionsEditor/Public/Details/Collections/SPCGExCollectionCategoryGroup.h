// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FAssetData;
class SBox;
class SWrapBox;
class SBorder;
class SImage;

DECLARE_DELEGATE_TwoParams(FOnCategoryRenamed, FName /*OldName*/, FName /*NewName*/);
DECLARE_DELEGATE_ThreeParams(FOnTileDropOnCategory, FName /*TargetCategory*/, const TArray<int32>& /*Indices*/, int32 /*InsertBeforeLocalIndex*/);
DECLARE_DELEGATE_TwoParams(FOnAssetDropOnCategory, FName /*TargetCategory*/, const TArray<FAssetData>& /*Assets*/);
DECLARE_DELEGATE_OneParam(FOnAddToCategory, FName /*Category*/);
DECLARE_DELEGATE_TwoParams(FOnCategoryExpansionChanged, FName /*Category*/, bool /*bIsExpanded*/);
DECLARE_DELEGATE_ThreeParams(FOnTileReorderInCategory, FName /*Category*/, const TArray<int32>& /*DraggedIndices*/, int32 /*InsertBeforeLocalIndex*/);

/**
 * Compound widget for a single category section in the grouped collection grid layout.
 * Contains an expandable header (with rename support) and a wrap box of tile widgets.
 * Acts as a drag-drop target for tile reordering between categories.
 */
class SPCGExCollectionCategoryGroup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExCollectionCategoryGroup)
		: _EntryCount(0)
		, _bIsCollapsed(false)
	{
	}

	SLATE_ARGUMENT(FName, CategoryName)
	SLATE_ARGUMENT(int32, EntryCount)
	SLATE_ARGUMENT(bool, bIsCollapsed)
	SLATE_EVENT(FOnCategoryRenamed, OnCategoryRenamed)
	SLATE_EVENT(FOnTileDropOnCategory, OnTileDropOnCategory)
	SLATE_EVENT(FOnAssetDropOnCategory, OnAssetDropOnCategory)
	SLATE_EVENT(FOnAddToCategory, OnAddToCategory)
	SLATE_EVENT(FOnCategoryExpansionChanged, OnExpansionChanged)
	SLATE_EVENT(FOnTileReorderInCategory, OnTileReorderInCategory)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Add a tile widget to the wrap box content area */
	void AddTile(const TSharedRef<SWidget>& TileWidget);

	/** Clear all tiles from the wrap box */
	void ClearTiles();

	/** Get the category name */
	FName GetCategoryName() const { return CategoryName; }

	/** Get collapse state */
	bool IsCollapsed() const { return bIsCollapsed; }

	// Drop target overrides
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& InDragDropEvent) override;

private:
	FName CategoryName;
	FOnCategoryRenamed OnCategoryRenamed;
	FOnTileDropOnCategory OnTileDropOnCategory;
	FOnAddToCategory OnAddToCategory;
	FOnAssetDropOnCategory OnAssetDropOnCategory;
	FOnCategoryExpansionChanged OnExpansionChanged;
	FOnTileReorderInCategory OnTileReorderInCategory;

	int32 DropInsertIndex = INDEX_NONE;

	TSharedPtr<SWrapBox> TilesWrapBox;
	TSharedPtr<SBorder> DropHighlightBorder;
	TSharedPtr<SBox> BodyContainer;
	TSharedPtr<SBox> InsertIndicator;
	TSharedPtr<SImage> CollapseArrow;
	bool bIsDragOver = false;
	bool bIsCollapsed = false;
};
