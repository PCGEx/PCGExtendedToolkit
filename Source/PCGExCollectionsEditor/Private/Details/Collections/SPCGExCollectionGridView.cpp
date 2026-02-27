// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridView.h"

#include "AssetThumbnail.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"

#include "InputCoreTypes.h"
#include "ScopedTransaction.h"

#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/FPCGExCollectionTileDragDropOp.h"
#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Details/Collections/SPCGExCollectionCategoryGroup.h"
#include "Details/Collections/SPCGExCollectionGridTile.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/TransactionObjectEvent.h"
#include "Modules/ModuleManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#pragma region SPCGExCollectionGridView

FReply SPCGExCollectionGridView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		if (!SelectedIndices.IsEmpty())
		{
			return OnDeleteSelected();
		}
	}

	if (InKeyEvent.GetKey() == EKeys::D && InKeyEvent.IsControlDown())
	{
		if (!SelectedIndices.IsEmpty())
		{
			return OnDuplicateSelected();
		}
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SPCGExCollectionGridView::Construct(const FArguments& InArgs)
{
	Collection = InArgs._Collection;
	ThumbnailPool = InArgs._ThumbnailPool;
	OnGetPickerWidget = InArgs._OnGetPickerWidget;
	TileSize = InArgs._TileSize;

	RebuildCategoryCache();

	// Create the IStructureDetailsView for editing a single entry struct
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;

	FStructureDetailsViewArgs StructArgs;

	TSharedPtr<FStructOnScope> NullStruct;
	StructDetailView = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, NullStruct);

	// Enforce VisibleAnywhere / read-only property flags.
	if (IDetailsView* InnerDetailsView = StructDetailView->GetDetailsView())
	{
		InnerDetailsView->SetIsPropertyReadOnlyDelegate(
			FIsPropertyReadOnly::CreateLambda([](const FPropertyAndParent& PropertyAndParent) -> bool
			{
				return PropertyAndParent.Property.HasAnyPropertyFlags(CPF_EditConst);
			}));
	}

	// Wire up property change callback to sync edits back to the collection
	StructDetailView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		this, &SPCGExCollectionGridView::OnDetailPropertyChanged);

	// Listen for undo/redo to fully refresh the grid when the collection is restored
	FCoreUObjectDelegates::OnObjectTransacted.AddSP(
		this, &SPCGExCollectionGridView::OnObjectTransacted);

	// Listen for external collection modifications (toolbar buttons, staging rebuild, etc.)
	FCoreUObjectDelegates::OnObjectModified.AddSP(
		this, &SPCGExCollectionGridView::OnObjectModified);

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		.PhysicalSplitterHandleSize(4.f)

		// Left pane: Grouped tile layout
		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(4.f)
				[
					SAssignNew(GroupScrollBox, SScrollBox)
					.OnUserScrolled(this, &SPCGExCollectionGridView::OnScrolled)
				]
			]

			// Pinned header overlay at top
			+ SOverlay::Slot()
			.VAlign(VAlign_Top)
			[
				SAssignNew(PinnedCategoryHeader, SBorder)
				.Visibility(EVisibility::Collapsed)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(6, 4))
				[
					SAssignNew(PinnedHeaderText, STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				]
			]
		]

		// Right pane: Detail panel
		+ SSplitter::Slot()
		.Value(0.35f)
		[
			SNew(SVerticalBox)

			// Action buttons (operate on tile selection)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(INVTEXT("Duplicate"))
					.ToolTipText(INVTEXT("Duplicate the selected entries"))
					.OnClicked(this, &SPCGExCollectionGridView::OnDuplicateSelected)
					.IsEnabled_Lambda([this]() { return !SelectedIndices.IsEmpty(); })
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(INVTEXT("Delete"))
					.ToolTipText(INVTEXT("Delete the selected entries"))
					.OnClicked(this, &SPCGExCollectionGridView::OnDeleteSelected)
					.IsEnabled_Lambda([this]() { return !SelectedIndices.IsEmpty(); })
				]
			]

			// Struct details view for the selected entry
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(4, 0, 4, 4)
			[
				StructDetailView->GetWidget().ToSharedRef()
			]
		]
	];

	// Build grouped layout
	RebuildGroupedLayout();
}

void SPCGExCollectionGridView::RebuildCategoryCache()
{
	SortedCategoryNames.Reset();
	CategoryToEntryIndices.Reset();
	VisualOrder.Reset();

	const UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	const int32 Num = Coll->NumEntries();

	// Group entries by Category
	for (int32 i = 0; i < Num; ++i)
	{
		const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(i);
		const FName Category = Result.IsValid() ? Result.Entry->Category : NAME_None;
		CategoryToEntryIndices.FindOrAdd(Category).Add(i);
	}

	// Sort category names alphabetically (NAME_None last)
	bool bHasUncategorized = false;
	for (auto& Pair : CategoryToEntryIndices)
	{
		if (Pair.Key.IsNone())
		{
			bHasUncategorized = true;
		}
		else
		{
			SortedCategoryNames.Add(Pair.Key);
		}
	}
	SortedCategoryNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	// Always add uncategorized as last category (persistent drop target)
	SortedCategoryNames.Add(NAME_None);
	if (!bHasUncategorized)
	{
		CategoryToEntryIndices.Add(NAME_None); // Empty array — still shows group
	}

	// Build visual order (flattened index list for shift-click range selection)
	for (const FName& CatName : SortedCategoryNames)
	{
		if (const TArray<int32>* Indices = CategoryToEntryIndices.Find(CatName))
		{
			VisualOrder.Append(*Indices);
		}
	}

	// Build combo options for category combobox on tiles
	if (!CategoryComboOptions.IsValid())
	{
		CategoryComboOptions = MakeShared<TArray<TSharedPtr<FName>>>();
	}
	CategoryComboOptions->Reset();
	CategoryComboOptions->Add(MakeShared<FName>(NAME_None)); // Uncategorized always first
	for (const FName& CatName : SortedCategoryNames)
	{
		if (!CatName.IsNone())
		{
			CategoryComboOptions->Add(MakeShared<FName>(CatName));
		}
	}
	// Add "New..." sentinel
	static const FName NewCategorySentinel("__PCGEx_NewCategory__");
	CategoryComboOptions->Add(MakeShared<FName>(NewCategorySentinel));
}

void SPCGExCollectionGridView::RebuildGroupedLayout()
{
	if (!GroupScrollBox.IsValid()) { return; }

	// Capture current collapse states before destroying old widgets
	for (const auto& Pair : CategoryGroupWidgets)
	{
		if (const TSharedPtr<SPCGExCollectionCategoryGroup>& Group = Pair.Value)
		{
			if (Group->IsCollapsed())
			{
				CollapsedCategories.Add(Pair.Key);
			}
			else
			{
				CollapsedCategories.Remove(Pair.Key);
			}
		}
	}

	GroupScrollBox->ClearChildren();
	CategoryGroupWidgets.Reset();
	ActiveTiles.Reset();

	for (const FName& CatName : SortedCategoryNames)
	{
		const TArray<int32>* Indices = CategoryToEntryIndices.Find(CatName);
		const int32 EntryCount = Indices ? Indices->Num() : 0;

		const bool bIsCollapsed = CollapsedCategories.Contains(CatName);

		TSharedPtr<SPCGExCollectionCategoryGroup> Group;

		GroupScrollBox->AddSlot()
			.Padding(0, 2)
			[
				SAssignNew(Group, SPCGExCollectionCategoryGroup)
				.CategoryName(CatName)
				.EntryCount(EntryCount)
				.bIsCollapsed(bIsCollapsed)
				.OnCategoryRenamed(FOnCategoryRenamed::CreateSP(this, &SPCGExCollectionGridView::OnCategoryRenamed))
				.OnTileDropOnCategory(FOnTileDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnTileDropOnCategory))
				.OnAssetDropOnCategory(FOnAssetDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnAssetDropOnCategory))
				.OnAddToCategory(FOnAddToCategory::CreateSP(this, &SPCGExCollectionGridView::OnAddToCategory))
				.OnExpansionChanged(FOnCategoryExpansionChanged::CreateSP(this, &SPCGExCollectionGridView::OnCategoryExpansionChanged))
				.OnTileReorderInCategory(FOnTileReorderInCategory::CreateSP(this, &SPCGExCollectionGridView::OnTileReorderInCategory))
			];

		CategoryGroupWidgets.Add(CatName, Group);

		// Skip tile creation for collapsed categories (lazy — created on expand)
		if (bIsCollapsed || !Indices) { continue; }

		// Create tiles for this category
		for (int32 CatIdx = 0; CatIdx < Indices->Num(); ++CatIdx)
		{
			const int32 EntryIdx = (*Indices)[CatIdx];

			TSharedPtr<SPCGExCollectionGridTile> Tile;

			TSharedRef<SWidget> TileWidget =
				SAssignNew(Tile, SPCGExCollectionGridTile)
				.ThumbnailPool(ThumbnailPool)
				.OnGetPickerWidget(OnGetPickerWidget)
				.TileSize(TileSize)
				.Collection(Collection)
				.EntryIndex(EntryIdx)
				.CategoryIndex(CatIdx)
				.CategoryOptions(CategoryComboOptions)
				.ThumbnailCachePtr(&ThumbnailCache)
				.OnTileClicked(FOnTileClicked::CreateSP(this, &SPCGExCollectionGridView::OnTileClicked))
				.OnTileDragDetected(FOnTileDragDetected::CreateSP(this, &SPCGExCollectionGridView::OnTileDragDetected));

			Group->AddTile(TileWidget);
			ActiveTiles.Add(EntryIdx, Tile);

			// Apply selection visual
			if (SelectedIndices.Contains(EntryIdx))
			{
				Tile->SetSelected(true);
			}
		}
	}

	// Prune stale thumbnail cache entries
	{
		TSet<FSoftObjectPath> ActivePaths;
		if (const UPCGExAssetCollection* Coll = Collection.Get())
		{
			for (const auto& Pair : ActiveTiles)
			{
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Pair.Key);
				if (Result.IsValid() && !Result.Entry->Staging.Path.IsNull())
				{
					ActivePaths.Add(Result.Entry->Staging.Path);
				}
			}
		}
		for (auto It = ThumbnailCache.CreateIterator(); It; ++It)
		{
			if (!ActivePaths.Contains(It->Key))
			{
				It.RemoveCurrent();
			}
		}
	}
}

void SPCGExCollectionGridView::IncrementalCategoryRefresh()
{
	if (!GroupScrollBox.IsValid()) { return; }

	// Capture collapse states
	for (const auto& Pair : CategoryGroupWidgets)
	{
		if (const TSharedPtr<SPCGExCollectionCategoryGroup>& Group = Pair.Value)
		{
			if (Group->IsCollapsed())
			{
				CollapsedCategories.Add(Pair.Key);
			}
			else
			{
				CollapsedCategories.Remove(Pair.Key);
			}
		}
	}

	// Snapshot tiles (keeps them alive during reparenting)
	TMap<int32, TSharedPtr<SPCGExCollectionGridTile>> PreviousTiles = MoveTemp(ActiveTiles);

	// Rebuild data-only category cache
	RebuildCategoryCache();

	// Clear layout containers
	GroupScrollBox->ClearChildren();
	CategoryGroupWidgets.Reset();
	ActiveTiles.Reset();

	// Rebuild category groups and reuse/create tiles
	for (const FName& CatName : SortedCategoryNames)
	{
		const TArray<int32>* Indices = CategoryToEntryIndices.Find(CatName);
		const int32 EntryCount = Indices ? Indices->Num() : 0;

		const bool bIsCollapsed = CollapsedCategories.Contains(CatName);

		TSharedPtr<SPCGExCollectionCategoryGroup> Group;

		GroupScrollBox->AddSlot()
			.Padding(0, 2)
			[
				SAssignNew(Group, SPCGExCollectionCategoryGroup)
				.CategoryName(CatName)
				.EntryCount(EntryCount)
				.bIsCollapsed(bIsCollapsed)
				.OnCategoryRenamed(FOnCategoryRenamed::CreateSP(this, &SPCGExCollectionGridView::OnCategoryRenamed))
				.OnTileDropOnCategory(FOnTileDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnTileDropOnCategory))
				.OnAssetDropOnCategory(FOnAssetDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnAssetDropOnCategory))
				.OnAddToCategory(FOnAddToCategory::CreateSP(this, &SPCGExCollectionGridView::OnAddToCategory))
				.OnExpansionChanged(FOnCategoryExpansionChanged::CreateSP(this, &SPCGExCollectionGridView::OnCategoryExpansionChanged))
				.OnTileReorderInCategory(FOnTileReorderInCategory::CreateSP(this, &SPCGExCollectionGridView::OnTileReorderInCategory))
			];

		CategoryGroupWidgets.Add(CatName, Group);

		// Skip tile creation for collapsed categories (lazy — created on expand)
		if (bIsCollapsed || !Indices) { continue; }

		for (int32 CatIdx = 0; CatIdx < Indices->Num(); ++CatIdx)
		{
			const int32 EntryIdx = (*Indices)[CatIdx];

			// Try to reuse existing tile
			if (TSharedPtr<SPCGExCollectionGridTile>* ExistingTile = PreviousTiles.Find(EntryIdx))
			{
				Group->AddTile(ExistingTile->ToSharedRef());
				ActiveTiles.Add(EntryIdx, *ExistingTile);
				continue;
			}

			// Fallback: create new tile
			TSharedPtr<SPCGExCollectionGridTile> Tile;

			TSharedRef<SWidget> TileWidget =
				SAssignNew(Tile, SPCGExCollectionGridTile)
				.ThumbnailPool(ThumbnailPool)
				.OnGetPickerWidget(OnGetPickerWidget)
				.TileSize(TileSize)
				.Collection(Collection)
				.EntryIndex(EntryIdx)
				.CategoryIndex(CatIdx)
				.CategoryOptions(CategoryComboOptions)
				.ThumbnailCachePtr(&ThumbnailCache)
				.OnTileClicked(FOnTileClicked::CreateSP(this, &SPCGExCollectionGridView::OnTileClicked))
				.OnTileDragDetected(FOnTileDragDetected::CreateSP(this, &SPCGExCollectionGridView::OnTileDragDetected));

			Group->AddTile(TileWidget);
			ActiveTiles.Add(EntryIdx, Tile);
		}
	}

	// Apply selection visuals
	ApplySelectionVisuals();
}

void SPCGExCollectionGridView::StructuralRefresh(EPCGExStructuralRefreshFlags Flags)
{
	ActiveTiles.Reset();

	if (EnumHasAnyFlags(Flags, EPCGExStructuralRefreshFlags::ClearSelection))
	{
		SelectedIndices.Reset();
		LastClickedIndex = INDEX_NONE;
	}

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();

	if (EnumHasAnyFlags(Flags, EPCGExStructuralRefreshFlags::ScrollToEnd))
	{
		if (GroupScrollBox.IsValid())
		{
			GroupScrollBox->ScrollToEnd();
		}
	}
}

void SPCGExCollectionGridView::RefreshGrid()
{
	RebuildCategoryCache();

	// Prune selection — remove indices that are no longer valid
	const int32 Num = Collection.IsValid() ? Collection->NumEntries() : 0;
	for (auto It = SelectedIndices.CreateIterator(); It; ++It)
	{
		if (*It < 0 || *It >= Num) { It.RemoveCurrent(); }
	}
	if (LastClickedIndex < 0 || LastClickedIndex >= Num)
	{
		LastClickedIndex = INDEX_NONE;
	}

	RebuildGroupedLayout();
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::RefreshDetailPanel()
{
	UpdateDetailForSelection();
}

TArray<int32> SPCGExCollectionGridView::GetSelectedIndices() const
{
	return SelectedIndices.Array();
}

// ── Selection ───────────────────────────────────────────────────────────────

void SPCGExCollectionGridView::SelectIndex(int32 Index, bool bCtrl, bool bShift)
{
	if (bShift && LastClickedIndex != INDEX_NONE)
	{
		// Range select in visual order
		const int32 StartPos = VisualOrder.Find(LastClickedIndex);
		const int32 EndPos = VisualOrder.Find(Index);

		if (StartPos != INDEX_NONE && EndPos != INDEX_NONE)
		{
			if (!bCtrl) { SelectedIndices.Reset(); }

			const int32 Lo = FMath::Min(StartPos, EndPos);
			const int32 Hi = FMath::Max(StartPos, EndPos);
			for (int32 i = Lo; i <= Hi; ++i)
			{
				SelectedIndices.Add(VisualOrder[i]);
			}
		}
		else
		{
			// Fallback if index not found in visual order
			SelectedIndices.Reset();
			SelectedIndices.Add(Index);
		}
	}
	else if (bCtrl)
	{
		// Toggle
		if (SelectedIndices.Contains(Index))
		{
			SelectedIndices.Remove(Index);
		}
		else
		{
			SelectedIndices.Add(Index);
		}
	}
	else
	{
		// Exclusive
		SelectedIndices.Reset();
		SelectedIndices.Add(Index);
	}

	LastClickedIndex = Index;
	ApplySelectionVisuals();
	NotifySelectionChanged();
}

void SPCGExCollectionGridView::ClearSelection()
{
	SelectedIndices.Reset();
	LastClickedIndex = INDEX_NONE;
	ApplySelectionVisuals();
	NotifySelectionChanged();
}

bool SPCGExCollectionGridView::IsSelected(int32 Index) const
{
	return SelectedIndices.Contains(Index);
}

void SPCGExCollectionGridView::NotifySelectionChanged()
{
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::ApplySelectionVisuals()
{
	for (const auto& Pair : ActiveTiles)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->SetSelected(SelectedIndices.Contains(Pair.Key));
		}
	}
}

// ── Tile callbacks ──────────────────────────────────────────────────────────

void SPCGExCollectionGridView::OnTileClicked(int32 Index, const FPointerEvent& MouseEvent)
{
	SelectIndex(Index, MouseEvent.IsControlDown(), MouseEvent.IsShiftDown());
}

FReply SPCGExCollectionGridView::OnTileDragDetected(int32 Index, const FPointerEvent& MouseEvent)
{
	if (SelectedIndices.IsEmpty()) { return FReply::Unhandled(); }

	// If dragged tile isn't selected, select it exclusively first
	if (!SelectedIndices.Contains(Index))
	{
		SelectIndex(Index, false, false);
	}

	// Determine source category
	FName SourceCategory = NAME_None;
	for (const auto& Pair : CategoryToEntryIndices)
	{
		if (Pair.Value.Contains(Index))
		{
			SourceCategory = Pair.Key;
			break;
		}
	}

	TArray<int32> DraggedIndices = SelectedIndices.Array();
	DraggedIndices.Sort();

	TSharedRef<FPCGExCollectionTileDragDropOp> DragOp = FPCGExCollectionTileDragDropOp::New(DraggedIndices, SourceCategory);
	return FReply::Handled().BeginDragDrop(DragOp);
}

// ── Category operations ─────────────────────────────────────────────────────

void SPCGExCollectionGridView::OnTileDropOnCategory(FName TargetCategory, const TArray<int32>& Indices)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || Indices.IsEmpty()) { return; }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Move Entries to Category"));
		Coll->Modify();

		for (int32 Index : Indices)
		{
			FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Index);
			if (Entry) { Entry->Category = TargetCategory; }
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	SelectedIndices.Reset();
	LastClickedIndex = INDEX_NONE;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::OnAssetDropOnCategory(FName TargetCategory, const TArray<FAssetData>& Assets)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || Assets.IsEmpty()) { return; }

	const int32 OldCount = Coll->NumEntries();

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Add Assets to Category"));
		Coll->Modify();

		Coll->EDITOR_AddBrowserSelectionTyped(Assets);

		// Set the Category on newly added entries
		const int32 NewCount = Coll->NumEntries();
		if (!TargetCategory.IsNone())
		{
			for (int32 i = OldCount; i < NewCount; ++i)
			{
				FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(i);
				if (Entry) { Entry->Category = TargetCategory; }
			}
		}
	}
	bIsBatchOperation = false;

	// Populate Staging.Path for new entries so thumbnails show correctly
	Coll->EDITOR_RebuildStagingData();

	StructuralRefresh();
}

void SPCGExCollectionGridView::OnCategoryRenamed(FName OldName, FName NewName)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || OldName == NewName) { return; }

	const int32 Num = Coll->NumEntries();

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Rename Category"));
		Coll->Modify();

		for (int32 i = 0; i < Num; ++i)
		{
			FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(i);
			if (Entry && Entry->Category == OldName)
			{
				Entry->Category = NewName;
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	StructuralRefresh();
}

void SPCGExCollectionGridView::OnAddToCategory(FName Category)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid()) { return; }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Add Entry to Category"));

		// Suppress staging rebuild — nothing to stage on an empty entry
		const bool bWasAutoRebuild = Coll->bAutoRebuildStaging;
		Coll->bAutoRebuildStaging = false;

		Coll->Modify();

		FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);
		const int32 NewIndex = ArrayHelper.AddValue();

		Coll->bAutoRebuildStaging = bWasAutoRebuild;

		// Set category on newly added entry
		FPCGExAssetCollectionEntry* NewEntry = Coll->EDITOR_GetMutableEntry(NewIndex);
		if (NewEntry) { NewEntry->Category = Category; }

		Coll->PostEditChange();

		// Select the new entry
		SelectedIndices.Reset();
		SelectedIndices.Add(NewIndex);
		LastClickedIndex = NewIndex;
	}
	bIsBatchOperation = false;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::OnCategoryExpansionChanged(FName Category, bool bIsExpanded)
{
	if (bIsExpanded)
	{
		CollapsedCategories.Remove(Category);
		PopulateCategoryTiles(Category);
	}
	else
	{
		CollapsedCategories.Add(Category);
	}
}

void SPCGExCollectionGridView::OnTileReorderInCategory(FName Category, const TArray<int32>& DraggedIndices, int32 InsertBeforeLocalIndex)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid()) { return; }

	const TArray<int32>* CatIndices = CategoryToEntryIndices.Find(Category);
	if (!CatIndices || CatIndices->Num() < 2) { return; }

	// Build the desired category order via remove-then-insert
	TSet<int32> DraggedSet(DraggedIndices);
	TArray<int32> NonDragged;
	TArray<int32> Dragged;

	for (int32 Idx : *CatIndices)
	{
		if (DraggedSet.Contains(Idx))
		{
			Dragged.Add(Idx);
		}
		else
		{
			NonDragged.Add(Idx);
		}
	}

	if (Dragged.IsEmpty()) { return; }

	// Adjust insertion index for removed dragged items before the insert point
	int32 AdjustedInsert = InsertBeforeLocalIndex;
	for (int32 i = 0; i < InsertBeforeLocalIndex && i < CatIndices->Num(); i++)
	{
		if (DraggedSet.Contains((*CatIndices)[i]))
		{
			AdjustedInsert--;
		}
	}
	AdjustedInsert = FMath::Clamp(AdjustedInsert, 0, NonDragged.Num());

	// Build desired order: NonDragged[0..Adj) + Dragged + NonDragged[Adj..end)
	TArray<int32> DesiredOrder;
	DesiredOrder.Reserve(CatIndices->Num());
	for (int32 i = 0; i < AdjustedInsert; i++) { DesiredOrder.Add(NonDragged[i]); }
	DesiredOrder.Append(Dragged);
	for (int32 i = AdjustedInsert; i < NonDragged.Num(); i++) { DesiredOrder.Add(NonDragged[i]); }

	// Check if order actually changed
	if (DesiredOrder == *CatIndices) { return; }

	UScriptStruct* EntryStruct = Access.InnerProp ? Access.InnerProp->Struct : nullptr;
	if (!EntryStruct) { return; }

	FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Reorder Entries"));
		Coll->Modify();

		// Selection-sort permutation using temp-buffer swap:
		const int32 StructSize = EntryStruct->GetStructureSize();
		TArray<uint8> Temp;
		Temp.SetNumUninitialized(StructSize);
		EntryStruct->InitializeStruct(Temp.GetData());

		TArray<int32> CurrentOrder = *CatIndices;
		for (int32 i = 0; i < DesiredOrder.Num(); i++)
		{
			if (CurrentOrder[i] == DesiredOrder[i]) { continue; }

			// Find where the desired entry currently is
			const int32 j = CurrentOrder.Find(DesiredOrder[i]);
			check(j != INDEX_NONE);

			// Swap array contents at the two raw array positions
			uint8* PtrA = ArrayHelper.GetRawPtr((*CatIndices)[i]);
			uint8* PtrB = ArrayHelper.GetRawPtr((*CatIndices)[j]);

			EntryStruct->CopyScriptStruct(Temp.GetData(), PtrA);
			EntryStruct->CopyScriptStruct(PtrA, PtrB);
			EntryStruct->CopyScriptStruct(PtrB, Temp.GetData());

			// Update tracking: the values at positions i and j swapped
			Swap(CurrentOrder[i], CurrentOrder[j]);
		}

		EntryStruct->DestroyStruct(Temp.GetData());

		Coll->PostEditChange();

		// Update selection to follow the dragged entries to their new positions
		TSet<int32> NewSelection;
		for (int32 i = 0; i < DesiredOrder.Num(); i++)
		{
			if (DraggedSet.Contains(DesiredOrder[i]))
			{
				NewSelection.Add((*CatIndices)[i]);
			}
		}
		SelectedIndices = MoveTemp(NewSelection);
		LastClickedIndex = SelectedIndices.Num() > 0 ? SelectedIndices.Array()[0] : INDEX_NONE;
	}
	bIsBatchOperation = false;

	// Copy affected indices — CatIndices pointer is invalidated by IncrementalCategoryRefresh
	const TArray<int32> AffectedIndices(*CatIndices);

	IncrementalCategoryRefresh();

	// Tiles were reused but thumbnails are cached from the old asset. Refresh them.
	for (int32 Idx : AffectedIndices)
	{
		if (const TSharedPtr<SPCGExCollectionGridTile> Tile = ActiveTiles.FindRef(Idx))
		{
			Tile->RefreshThumbnail();
		}
	}

	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::PopulateCategoryTiles(FName Category)
{
	TSharedPtr<SPCGExCollectionCategoryGroup>* GroupPtr = CategoryGroupWidgets.Find(Category);
	if (!GroupPtr || !GroupPtr->IsValid()) { return; }

	const TArray<int32>* Indices = CategoryToEntryIndices.Find(Category);
	if (!Indices || Indices->IsEmpty()) { return; }

	// Check if tiles already exist for this category
	bool bAlreadyPopulated = false;
	for (int32 EntryIdx : *Indices)
	{
		if (ActiveTiles.Contains(EntryIdx))
		{
			bAlreadyPopulated = true;
			break;
		}
	}
	if (bAlreadyPopulated) { return; }

	TSharedPtr<SPCGExCollectionCategoryGroup>& Group = *GroupPtr;

	for (int32 CatIdx = 0; CatIdx < Indices->Num(); ++CatIdx)
	{
		const int32 EntryIdx = (*Indices)[CatIdx];

		TSharedPtr<SPCGExCollectionGridTile> Tile;

		TSharedRef<SWidget> TileWidget =
			SAssignNew(Tile, SPCGExCollectionGridTile)
			.ThumbnailPool(ThumbnailPool)
			.OnGetPickerWidget(OnGetPickerWidget)
			.TileSize(TileSize)
			.Collection(Collection)
			.EntryIndex(EntryIdx)
			.CategoryIndex(CatIdx)
			.CategoryOptions(CategoryComboOptions)
			.ThumbnailCachePtr(&ThumbnailCache)
			.OnTileClicked(FOnTileClicked::CreateSP(this, &SPCGExCollectionGridView::OnTileClicked))
			.OnTileDragDetected(FOnTileDragDetected::CreateSP(this, &SPCGExCollectionGridView::OnTileDragDetected));

		Group->AddTile(TileWidget);
		ActiveTiles.Add(EntryIdx, Tile);

		if (SelectedIndices.Contains(EntryIdx))
		{
			Tile->SetSelected(true);
		}
	}
}

// ── Detail panel management ─────────────────────────────────────────────────

void SPCGExCollectionGridView::UpdateDetailForSelection()
{
	if (SelectedIndices.IsEmpty())
	{
		CurrentDetailIndex = INDEX_NONE;
		CurrentStructScope.Reset();
		if (StructDetailView.IsValid())
		{
			TSharedPtr<FStructOnScope> NullStruct;
			StructDetailView->SetStructureData(NullStruct);
		}
		return;
	}

	// Show the last-clicked entry if it's in the selection, otherwise first
	int32 Index = INDEX_NONE;
	if (LastClickedIndex != INDEX_NONE && SelectedIndices.Contains(LastClickedIndex))
	{
		Index = LastClickedIndex;
	}
	else
	{
		TArray<int32> Sorted = SelectedIndices.Array();
		Sorted.Sort();
		Index = Sorted[0];
	}

	UScriptStruct* EntryStruct = GetEntryScriptStruct();
	uint8* EntryPtr = GetEntryRawPtr(Index);

	if (!EntryStruct || !EntryPtr) { return; }

	// Create FStructOnScope with a copy of the entry data
	CurrentStructScope = MakeShared<FStructOnScope>(EntryStruct);
	EntryStruct->CopyScriptStruct(CurrentStructScope->GetStructMemory(), EntryPtr);
	CurrentDetailIndex = Index;

	if (StructDetailView.IsValid())
	{
		StructDetailView->SetStructureData(CurrentStructScope);
	}
}

void SPCGExCollectionGridView::SyncStructToCollection(const FProperty* ChangedMemberProperty)
{
	if (!CurrentStructScope.IsValid() || CurrentDetailIndex == INDEX_NONE) { return; }

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	UScriptStruct* EntryStruct = GetEntryScriptStruct();
	if (!EntryStruct) { return; }

	uint8* PrimaryPtr = GetEntryRawPtr(CurrentDetailIndex);
	if (!PrimaryPtr) { return; }

	const uint8* SrcData = CurrentStructScope->GetStructMemory();

	Coll->Modify();

	// Copy entire struct back to the primary entry (it's the editing copy)
	EntryStruct->CopyScriptStruct(PrimaryPtr, SrcData);

	// For multi-select: propagate ONLY the changed property to other entries
	if (ChangedMemberProperty)
	{
		const int32 Offset = ChangedMemberProperty->GetOffset_ForInternal();
		TArray<int32> Selected = GetSelectedIndices();
		for (int32 OtherIndex : Selected)
		{
			if (OtherIndex == CurrentDetailIndex) { continue; }
			uint8* OtherPtr = GetEntryRawPtr(OtherIndex);
			if (OtherPtr)
			{
				ChangedMemberProperty->CopyCompleteValue(OtherPtr + Offset, SrcData + Offset);
			}
		}
	}

	Coll->PostEditChange();
}

void SPCGExCollectionGridView::OnDetailPropertyChanged(const FPropertyChangedEvent& Event)
{
	bIsSyncing = true;
	SyncStructToCollection(Event.MemberProperty);
	bIsSyncing = false;

	// Check if category changed — need to rebuild groups
	static const FName CategoryPropertyName = GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category);
	if (Event.MemberProperty && Event.MemberProperty->GetFName() == CategoryPropertyName)
	{
		// Defer grid refresh to avoid destroying widgets during their own event handling
		if (!bPendingCategoryRefresh)
		{
			bPendingCategoryRefresh = true;
			RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
				[this](double, float) -> EActiveTimerReturnType
				{
					bPendingCategoryRefresh = false;
					IncrementalCategoryRefresh();
					UpdateDetailForSelection();
					return EActiveTimerReturnType::Stop;
				}));
		}
		return;
	}

	// Refresh only the selected tile(s) thumbnails
	TArray<int32> Selected = GetSelectedIndices();
	for (int32 Index : Selected)
	{
		if (TSharedPtr<SPCGExCollectionGridTile> Tile = ActiveTiles.FindRef(Index))
		{
			Tile->RefreshThumbnail();
		}
	}
}

// ── Entry struct reflection helpers ─────────────────────────────────────────

UScriptStruct* SPCGExCollectionGridView::GetEntryScriptStruct() const
{
	const UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return nullptr; }

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp) { return nullptr; }

	FStructProperty* InnerProp = CastField<FStructProperty>(ArrayProp->Inner);
	return InnerProp ? InnerProp->Struct : nullptr;
}

uint8* SPCGExCollectionGridView::GetEntryRawPtr(int32 Index) const
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || Index < 0) { return nullptr; }

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp) { return nullptr; }

	void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);

	if (Index >= ArrayHelper.Num()) { return nullptr; }
	return ArrayHelper.GetRawPtr(Index);
}

SPCGExCollectionGridView::FEntriesArrayAccess SPCGExCollectionGridView::GetEntriesAccess() const
{
	FEntriesArrayAccess Result;

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return Result; }

	Result.ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!Result.ArrayProp) { return Result; }

	Result.InnerProp = CastField<FStructProperty>(Result.ArrayProp->Inner);
	Result.ArrayData = Result.ArrayProp->ContainerPtrToValuePtr<void>(Coll);

	return Result;
}

// ── Entry operations ────────────────────────────────────────────────────────

FReply SPCGExCollectionGridView::OnAddEntry()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid()) { return FReply::Handled(); }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Add Collection Entry"));

		// Suppress staging rebuild — nothing to stage on an empty entry
		const bool bWasAutoRebuild = Coll->bAutoRebuildStaging;
		Coll->bAutoRebuildStaging = false;

		Coll->Modify();

		FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);
		const int32 NewIndex = ArrayHelper.AddValue();

		Coll->bAutoRebuildStaging = bWasAutoRebuild;
		Coll->PostEditChange();

		// Select the new entry
		SelectedIndices.Reset();
		SelectedIndices.Add(NewIndex);
		LastClickedIndex = NewIndex;
	}
	bIsBatchOperation = false;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();
	if (GroupScrollBox.IsValid()) { GroupScrollBox->ScrollToEnd(); }

	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDuplicateSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid()) { return FReply::Handled(); }

	UScriptStruct* EntryStruct = Access.InnerProp ? Access.InnerProp->Struct : nullptr;
	if (!EntryStruct) { return FReply::Handled(); }

	TArray<int32> Selected = SelectedIndices.Array();
	Selected.Sort();
	if (Selected.IsEmpty()) { return FReply::Handled(); }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Duplicate Collection Entries"));
		Coll->Modify();

		FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);

		// Duplicate in reverse order to preserve source indices.
		for (int32 i = Selected.Num() - 1; i >= 0; --i)
		{
			const int32 SrcIndex = Selected[i];
			const int32 InsertAt = SrcIndex + 1;

			ArrayHelper.InsertValues(InsertAt, 1);

			// Copy source to newly inserted element
			uint8* SrcPtr = ArrayHelper.GetRawPtr(SrcIndex);
			uint8* DstPtr = ArrayHelper.GetRawPtr(InsertAt);
			EntryStruct->CopyScriptStruct(DstPtr, SrcPtr);
		}

		Coll->PostEditChange();

		// Compute final positions of duplicates:
		// For Selected[k] (sorted ascending, 0-based), its duplicate ends up at Selected[k] + k + 1
		// because k earlier duplicates were inserted before it.
		SelectedIndices.Reset();
		LastClickedIndex = INDEX_NONE;
		for (int32 k = 0; k < Selected.Num(); k++)
		{
			const int32 FinalPos = Selected[k] + k + 1;
			SelectedIndices.Add(FinalPos);
			if (LastClickedIndex == INDEX_NONE) { LastClickedIndex = FinalPos; }
		}
	}
	bIsBatchOperation = false;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();

	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDeleteSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid()) { return FReply::Handled(); }

	TArray<int32> Selected = SelectedIndices.Array();
	Selected.Sort();
	if (Selected.IsEmpty()) { return FReply::Handled(); }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Delete Collection Entries"));
		Coll->Modify();

		FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);

		// Delete in reverse order to preserve earlier indices
		for (int32 i = Selected.Num() - 1; i >= 0; --i)
		{
			ArrayHelper.RemoveValues(Selected[i], 1);
		}

		Coll->PostEditChange();

		SelectedIndices.Reset();
		LastClickedIndex = INDEX_NONE;
	}
	bIsBatchOperation = false;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();

	return FReply::Handled();
}

// ── Drag-drop ───────────────────────────────────────────────────────────────

FReply SPCGExCollectionGridView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	if (InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		return FReply::Handled();
	}
	if (InDragDropEvent.GetOperationAs<FPCGExCollectionTileDragDropOp>())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPCGExCollectionGridView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	// Content Browser asset drops outside any category group = add to uncategorized
	if (const TSharedPtr<FAssetDragDropOp> AssetOp = InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		const TArray<FAssetData>& Assets = AssetOp->GetAssets();
		if (!Assets.IsEmpty())
		{
			OnAssetDropOnCategory(NAME_None, Assets);
			return FReply::Handled();
		}
	}

	// Internal tile drops outside any category group = move to uncategorized
	if (const TSharedPtr<FPCGExCollectionTileDragDropOp> TileOp = InDragDropEvent.GetOperationAs<FPCGExCollectionTileDragDropOp>())
	{
		OnTileDropOnCategory(NAME_None, TileOp->DraggedIndices);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ── Undo/redo and external modification ─────────────────────────────────────

void SPCGExCollectionGridView::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (Object == Collection.Get() && Event.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		StructuralRefresh(EPCGExStructuralRefreshFlags::ClearSelection);
	}
}

void SPCGExCollectionGridView::OnObjectModified(UObject* Object)
{
	if (Object != Collection.Get()) { return; }
	if (bIsSyncing || bIsBatchOperation) { return; }
	if (bPendingExternalRefresh) { return; } // Already scheduled

	// Defer to next tick — Modify() fires BEFORE changes are applied,
	// so the entry count / data hasn't been updated yet.
	bPendingExternalRefresh = true;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[this](double, float) -> EActiveTimerReturnType
		{
			bPendingExternalRefresh = false;

			if (bIsSyncing || bIsBatchOperation) { return EActiveTimerReturnType::Stop; }

			const int32 CurrentCount = Collection.IsValid() ? Collection->NumEntries() : 0;
			if (CurrentCount != VisualOrder.Num())
			{
				// Entry count changed externally — rebuild staging for new entries
				if (UPCGExAssetCollection* Coll = Collection.Get())
				{
					bIsBatchOperation = true;
					Coll->EDITOR_RebuildStagingData();
					bIsBatchOperation = false;
				}
				StructuralRefresh();
			}
			else
			{
				// Data changed but count same (staging rebuild, sort, etc.)
				UpdateDetailForSelection();

				// Refresh tile thumbnails in case staging paths changed
				for (const auto& Pair : ActiveTiles)
				{
					if (Pair.Value.IsValid())
					{
						Pair.Value->RefreshThumbnail();
					}
				}

				// Also do category refresh in case categories changed
				IncrementalCategoryRefresh();
			}
			return EActiveTimerReturnType::Stop;
		}));
}

void SPCGExCollectionGridView::OnScrolled(float ScrollOffset)
{
	// No pinned header if only one category exists
	if (SortedCategoryNames.Num() <= 1)
	{
		if (PinnedCategoryHeader.IsValid())
		{
			PinnedCategoryHeader->SetVisibility(EVisibility::Collapsed);
		}
		PinnedCategoryName = NAME_None;
		return;
	}

	FName TopCategory = NAME_None;
	bool bShowPinned = false;

	for (const FName& CatName : SortedCategoryNames)
	{
		if (TSharedPtr<SPCGExCollectionCategoryGroup>* GroupPtr = CategoryGroupWidgets.Find(CatName))
		{
			const FGeometry& ScrollGeo = GroupScrollBox->GetCachedGeometry();
			const FGeometry& GroupGeo = (*GroupPtr)->GetCachedGeometry();

			if (!ScrollGeo.GetLocalSize().IsNearlyZero() && !GroupGeo.GetLocalSize().IsNearlyZero())
			{
				const FVector2D GroupLocalPos = ScrollGeo.AbsoluteToLocal(GroupGeo.GetAbsolutePosition());

				// Use a small threshold to avoid sub-pixel false positives
				if (GroupLocalPos.Y < -2.0f)
				{
					TopCategory = CatName;
					bShowPinned = true;
				}
				else
				{
					break;
				}
			}
		}
	}

	if (bShowPinned && TopCategory != PinnedCategoryName)
	{
		PinnedCategoryName = TopCategory;
		const FText DisplayName = TopCategory.IsNone() ? INVTEXT("Uncategorized") : FText::FromName(TopCategory);
		PinnedHeaderText->SetText(DisplayName);
	}

	if (!bShowPinned)
	{
		PinnedCategoryName = NAME_None;
	}

	if (PinnedCategoryHeader.IsValid())
	{
		PinnedCategoryHeader->SetVisibility(bShowPinned ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	}
}

#pragma endregion
