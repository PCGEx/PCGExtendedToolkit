// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridView.h"

#include "AssetThumbnail.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

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
	InitRowGenerator();

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

	if (bHasUncategorized)
	{
		SortedCategoryNames.Add(NAME_None);
	}

	// If no entries at all, ensure we have at least the uncategorized group
	if (SortedCategoryNames.IsEmpty() && Num == 0)
	{
		SortedCategoryNames.Add(NAME_None);
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
		if (!Indices || Indices->IsEmpty()) { continue; }

		const bool bIsCollapsed = CollapsedCategories.Contains(CatName);

		TSharedPtr<SPCGExCollectionCategoryGroup> Group;

		GroupScrollBox->AddSlot()
			.Padding(0, 2)
			[
				SAssignNew(Group, SPCGExCollectionCategoryGroup)
				.CategoryName(CatName)
				.EntryCount(Indices->Num())
				.bIsCollapsed(bIsCollapsed)
				.OnCategoryRenamed(FOnCategoryRenamed::CreateSP(this, &SPCGExCollectionGridView::OnCategoryRenamed))
				.OnTileDropOnCategory(FOnTileDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnTileDropOnCategory))
				.OnAssetDropOnCategory(FOnAssetDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnAssetDropOnCategory))
				.OnAddToCategory(FOnAddToCategory::CreateSP(this, &SPCGExCollectionGridView::OnAddToCategory))
			];

		CategoryGroupWidgets.Add(CatName, Group);

		// Create tiles for this category
		for (int32 CatIdx = 0; CatIdx < Indices->Num(); ++CatIdx)
		{
			const int32 EntryIdx = (*Indices)[CatIdx];

			// Get the entry property handle from the array
			TSharedPtr<IPropertyHandle> EntryHandle;
			if (EntriesArrayHandle.IsValid())
			{
				TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
				if (ArrayHandle.IsValid())
				{
					uint32 NumElements = 0;
					ArrayHandle->GetNumElements(NumElements);
					if (static_cast<uint32>(EntryIdx) < NumElements)
					{
						EntryHandle = ArrayHandle->GetElement(EntryIdx);
					}
				}
			}

			if (!EntryHandle.IsValid() || !EntryHandle->GetProperty()) { continue; }

			TSharedPtr<SPCGExCollectionGridTile> Tile;

			TSharedRef<SWidget> TileWidget =
				SAssignNew(Tile, SPCGExCollectionGridTile)
				.EntryHandle(EntryHandle)
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
		if (!Indices || Indices->IsEmpty()) { continue; }

		const bool bIsCollapsed = CollapsedCategories.Contains(CatName);

		TSharedPtr<SPCGExCollectionCategoryGroup> Group;

		GroupScrollBox->AddSlot()
			.Padding(0, 2)
			[
				SAssignNew(Group, SPCGExCollectionCategoryGroup)
				.CategoryName(CatName)
				.EntryCount(Indices->Num())
				.bIsCollapsed(bIsCollapsed)
				.OnCategoryRenamed(FOnCategoryRenamed::CreateSP(this, &SPCGExCollectionGridView::OnCategoryRenamed))
				.OnTileDropOnCategory(FOnTileDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnTileDropOnCategory))
				.OnAssetDropOnCategory(FOnAssetDropOnCategory::CreateSP(this, &SPCGExCollectionGridView::OnAssetDropOnCategory))
				.OnAddToCategory(FOnAddToCategory::CreateSP(this, &SPCGExCollectionGridView::OnAddToCategory))
			];

		CategoryGroupWidgets.Add(CatName, Group);

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
			TSharedPtr<IPropertyHandle> EntryHandle;
			if (EntriesArrayHandle.IsValid())
			{
				TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
				if (ArrayHandle.IsValid())
				{
					uint32 NumElements = 0;
					ArrayHandle->GetNumElements(NumElements);
					if (static_cast<uint32>(EntryIdx) < NumElements)
					{
						EntryHandle = ArrayHandle->GetElement(EntryIdx);
					}
				}
			}

			if (!EntryHandle.IsValid() || !EntryHandle->GetProperty()) { continue; }

			TSharedPtr<SPCGExCollectionGridTile> Tile;

			TSharedRef<SWidget> TileWidget =
				SAssignNew(Tile, SPCGExCollectionGridTile)
				.EntryHandle(EntryHandle)
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
			uint8* EntryPtr = GetEntryRawPtr(Index);
			if (!EntryPtr) { continue; }
			reinterpret_cast<FPCGExAssetCollectionEntry*>(EntryPtr)->Category = TargetCategory;
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
				uint8* EntryPtr = GetEntryRawPtr(i);
				if (EntryPtr)
				{
					reinterpret_cast<FPCGExAssetCollectionEntry*>(EntryPtr)->Category = TargetCategory;
				}
			}
		}
	}
	bIsBatchOperation = false;

	// Populate Staging.Path for new entries so thumbnails show correctly
	Coll->EDITOR_RebuildStagingData();

	InitRowGenerator();
	RefreshGrid();
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
			uint8* EntryPtr = GetEntryRawPtr(i);
			if (!EntryPtr) { continue; }
			FPCGExAssetCollectionEntry* Entry = reinterpret_cast<FPCGExAssetCollectionEntry*>(EntryPtr);
			if (Entry->Category == OldName)
			{
				Entry->Category = NewName;
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::OnAddToCategory(FName Category)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp) { return; }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Add Entry to Category"));
		Coll->Modify();

		// Suppress staging rebuild — nothing to stage on an empty entry
		const bool bWasAutoRebuild = Coll->bAutoRebuildStaging;
		Coll->bAutoRebuildStaging = false;

		void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
		ArrayHelper.AddValues(1);

		// Set category on the new entry
		const int32 NewIndex = ArrayHelper.Num() - 1;
		uint8* NewEntryPtr = ArrayHelper.GetRawPtr(NewIndex);
		if (NewEntryPtr)
		{
			reinterpret_cast<FPCGExAssetCollectionEntry*>(NewEntryPtr)->Category = Category;
		}

		Coll->PostEditChange();
		Coll->bAutoRebuildStaging = bWasAutoRebuild;
	}
	bIsBatchOperation = false;

	// Structural array change — rebuild RowGenerator for fresh property handles
	InitRowGenerator();

	// Select the newly added entry
	const int32 NewIndex = Coll->NumEntries() - 1;
	SelectedIndices.Reset();
	SelectedIndices.Add(NewIndex);
	LastClickedIndex = NewIndex;

	RefreshGrid();
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

	// Refresh only the selected tile(s) thumbnails — property value widgets
	// on tiles auto-update via their live IPropertyHandle bindings.
	TArray<int32> Selected = GetSelectedIndices();
	for (int32 Index : Selected)
	{
		if (TSharedPtr<SPCGExCollectionGridTile> Tile = ActiveTiles.FindRef(Index))
		{
			Tile->RefreshThumbnail();
		}
	}
}

void SPCGExCollectionGridView::OnRowGeneratorPropertyChanged(const FPropertyChangedEvent& Event)
{
	// Skip during batch operations or detail panel sync
	if (bIsSyncing || bIsBatchOperation) { return; }

	// Check if the Category property changed — need to rebuild groups
	static const FName CategoryPropertyName = GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category);
	if (Event.Property && Event.Property->GetFName() == CategoryPropertyName)
	{
		// Defer grid refresh to avoid destroying tiles during their own event handling
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

	// A tile control changed — re-sync the detail panel from collection data
	UpdateDetailForSelection();
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

// ── Property row generator ──────────────────────────────────────────────────

static bool FindEntriesHandleRecursive(const TArray<TSharedRef<IDetailTreeNode>>& Nodes, TSharedPtr<IPropertyHandle>& OutHandle)
{
	for (const TSharedRef<IDetailTreeNode>& Node : Nodes)
	{
		TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
		if (Handle.IsValid() && Handle->GetProperty() && Handle->GetProperty()->GetFName() == FName("Entries"))
		{
			OutHandle = Handle;
			return true;
		}

		// Recurse into children (root nodes may be categories)
		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children);
		if (!Children.IsEmpty() && FindEntriesHandleRecursive(Children, OutHandle))
		{
			return true;
		}
	}
	return false;
}

void SPCGExCollectionGridView::InitRowGenerator()
{
	EntriesArrayHandle.Reset();

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	// Create a row generator to get live property handles for entry operations (add/duplicate/delete)
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FPropertyRowGeneratorArgs Args;
	RowGenerator = PropertyModule.CreatePropertyRowGenerator(Args);

	if (RowGenerator.IsValid())
	{
		RowGenerator->SetObjects({Coll});

		// Listen for property changes from tile controls to sync the detail panel
		RowGenerator->OnFinishedChangingProperties().AddSP(
			this, &SPCGExCollectionGridView::OnRowGeneratorPropertyChanged);

		// Find the "Entries" property handle — may be nested under a category node
		const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = RowGenerator->GetRootTreeNodes();
		FindEntriesHandleRecursive(RootNodes, EntriesArrayHandle);
	}
}

// ── Entry operations ────────────────────────────────────────────────────────

FReply SPCGExCollectionGridView::OnAddEntry()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp) { return FReply::Handled(); }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Add Collection Entry"));
		Coll->Modify();

		// Suppress staging rebuild — nothing to stage on an empty entry
		const bool bWasAutoRebuild = Coll->bAutoRebuildStaging;
		Coll->bAutoRebuildStaging = false;

		void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);
		ArrayHelper.AddValues(1);

		Coll->PostEditChange();
		Coll->bAutoRebuildStaging = bWasAutoRebuild;
	}
	bIsBatchOperation = false;

	// Structural array change — rebuild RowGenerator for fresh property handles
	InitRowGenerator();

	// Select the newly added entry
	const int32 NewIndex = Coll->NumEntries() - 1;
	SelectedIndices.Reset();
	SelectedIndices.Add(NewIndex);
	LastClickedIndex = NewIndex;

	RefreshGrid();

	// Scroll to the new entry (it's uncategorized, at the end)
	if (GroupScrollBox.IsValid())
	{
		GroupScrollBox->ScrollToEnd();
	}

	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDuplicateSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	TArray<int32> Selected = SelectedIndices.Array();
	Selected.Sort();
	if (Selected.IsEmpty()) { return FReply::Handled(); }

	UScriptStruct* EntryStruct = GetEntryScriptStruct();
	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!EntryStruct || !ArrayProp) { return FReply::Handled(); }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Duplicate Collection Entries"));
		Coll->Modify();

		void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);

		// Duplicate in reverse order to preserve indices
		for (int32 i = Selected.Num() - 1; i >= 0; --i)
		{
			const int32 Index = Selected[i];
			if (Index < ArrayHelper.Num())
			{
				const int32 NewIndex = Index + 1;
				ArrayHelper.InsertValues(NewIndex);
				EntryStruct->CopyScriptStruct(
					ArrayHelper.GetRawPtr(NewIndex),
					ArrayHelper.GetRawPtr(Index));
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	// Structural array change — rebuild RowGenerator for fresh property handles
	InitRowGenerator();

	SelectedIndices.Reset();
	LastClickedIndex = INDEX_NONE;

	RefreshGrid();
	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDeleteSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	TArray<int32> Selected = SelectedIndices.Array();
	Selected.Sort();
	if (Selected.IsEmpty()) { return FReply::Handled(); }

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp) { return FReply::Handled(); }

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Delete Collection Entries"));
		Coll->Modify();

		void* ArrayData = ArrayProp->ContainerPtrToValuePtr<void>(Coll);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayData);

		// Delete in reverse order to preserve indices
		for (int32 i = Selected.Num() - 1; i >= 0; --i)
		{
			const int32 Index = Selected[i];
			if (Index < ArrayHelper.Num())
			{
				ArrayHelper.RemoveValues(Index, 1);
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	// Structural array change — rebuild RowGenerator for fresh property handles
	InitRowGenerator();

	SelectedIndices.Reset();
	LastClickedIndex = INDEX_NONE;

	RefreshGrid();
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
		// Undo/redo replaces the object's internal state — RowGenerator handles become stale
		InitRowGenerator();
		SelectedIndices.Reset();
		LastClickedIndex = INDEX_NONE;
		RefreshGrid();
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
				InitRowGenerator();
				RefreshGrid();
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
			}
			return EActiveTimerReturnType::Stop;
		}));
}

void SPCGExCollectionGridView::OnScrolled(float ScrollOffset)
{
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

				if (GroupLocalPos.Y < 0)
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
