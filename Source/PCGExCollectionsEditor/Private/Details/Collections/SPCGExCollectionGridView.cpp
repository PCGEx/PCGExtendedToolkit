// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridView.h"

#include "AssetThumbnail.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"

#include "InputCoreTypes.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionHelpers.h"
#include "Details/Collections/FPCGExCollectionTileDragDropOp.h"
#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Details/Collections/SPCGExCollectionCategoryGroup.h"
#include "Details/Collections/SPCGExCollectionGridTile.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/TransactionObjectEvent.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
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
	PushOptions = InArgs._PushOptions;

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
					.IsEnabled_Lambda([this]()
					{
						return !SelectedIndices.IsEmpty();
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(INVTEXT("Delete"))
					.ToolTipText(INVTEXT("Delete the selected entries"))
					.OnClicked(this, &SPCGExCollectionGridView::OnDeleteSelected)
					.IsEnabled_Lambda([this]()
					{
						return !SelectedIndices.IsEmpty();
					})
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
					.ComboButtonStyle(&FAppStyle::Get(), "SimpleComboButton")
					.HasDownArrow(true)
					.ContentPadding(FMargin(4, 4))
					.ToolTipText(INVTEXT("Push to selection\nCopy properties from the active entry (the one currently shown in this panel) to every other entry in the multi-selection."))
					.Visibility_Lambda([this]()
					{
						return (SelectedIndices.Num() > 1 && CurrentDetailIndex != INDEX_NONE && !PushOptions.IsEmpty())
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(INVTEXT("Push to selection"))
					]
					.OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
					{
						TSharedRef<SVerticalBox> MenuBox = SNew(SVerticalBox);
						for (const PCGExAssetCollectionEditor::FPushOption& Option : PushOptions)
						{
							MenuBox->AddSlot()
							       .AutoHeight()
							       .Padding(4, 2)
							[
								SNew(SButton)
								.Text(Option.Label)
								.ToolTipText(Option.Tooltip)
								.OnClicked_Lambda([this, Option]()
								{
									ExecutePush(Option);
									FSlateApplication::Get().DismissAllMenus();
									return FReply::Handled();
								})
							];
						}
						return MenuBox;
					})
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
	if (!Coll)
	{
		return;
	}

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
	SortedCategoryNames.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});

	// Always add uncategorized as last category (persistent drop target)
	SortedCategoryNames.Add(NAME_None);
	if (!bHasUncategorized)
	{
		CategoryToEntryIndices.Add(NAME_None); // Empty array -- still shows group
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
	if (!GroupScrollBox.IsValid())
	{
		return;
	}

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

		// Skip tile creation for collapsed categories (lazy -- created on expand)
		if (bIsCollapsed || !Indices)
		{
			continue;
		}

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
				.BatchFlagPtr(&bIsBatchOperation)
				.OnTileClicked(FOnTileClicked::CreateSP(this, &SPCGExCollectionGridView::OnTileClicked))
				.OnTileDragDetected(FOnTileDragDetected::CreateSP(this, &SPCGExCollectionGridView::OnTileDragDetected))
				.OnTileEntryChanged(FOnTileEntryChanged::CreateSP(this, &SPCGExCollectionGridView::OnTileEntryChanged));

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
				if (Result.IsValid())
				{
					const FSoftObjectPath ThumbnailPath = Result.Entry->EDITOR_GetThumbnailAssetPath();
					if (!ThumbnailPath.IsNull())
					{
						ActivePaths.Add(ThumbnailPath);
					}
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
	if (!GroupScrollBox.IsValid())
	{
		return;
	}

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

		// Skip tile creation for collapsed categories (lazy -- created on expand)
		if (bIsCollapsed || !Indices)
		{
			continue;
		}

		for (int32 CatIdx = 0; CatIdx < Indices->Num(); ++CatIdx)
		{
			const int32 EntryIdx = (*Indices)[CatIdx];

			// Try to reuse existing tile
			if (TSharedPtr<SPCGExCollectionGridTile>* ExistingTile = PreviousTiles.Find(EntryIdx))
			{
				Group->AddTile(ExistingTile->ToSharedRef());
				ActiveTiles.Add(EntryIdx, *ExistingTile);
				(*ExistingTile)->RefreshThumbnail(); // Data at this index may have shifted
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
				.BatchFlagPtr(&bIsBatchOperation)
				.OnTileClicked(FOnTileClicked::CreateSP(this, &SPCGExCollectionGridView::OnTileClicked))
				.OnTileDragDetected(FOnTileDragDetected::CreateSP(this, &SPCGExCollectionGridView::OnTileDragDetected))
				.OnTileEntryChanged(FOnTileEntryChanged::CreateSP(this, &SPCGExCollectionGridView::OnTileEntryChanged));

			Group->AddTile(TileWidget);
			ActiveTiles.Add(EntryIdx, Tile);
		}
	}

	// Apply selection visuals
	ApplySelectionVisuals();
}

void SPCGExCollectionGridView::StructuralRefresh(EPCGExStructuralRefreshFlags Flags)
{
	// Don't clear ActiveTiles here -- IncrementalCategoryRefresh snapshots them for tile reuse

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

	// Prune selection -- remove indices that are no longer valid
	const int32 Num = Collection.IsValid() ? Collection->NumEntries() : 0;
	for (auto It = SelectedIndices.CreateIterator(); It; ++It)
	{
		if (*It < 0 || *It >= Num)
		{
			It.RemoveCurrent();
		}
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

	// Only reached on filter toggles (ForceRefreshTabs). UpdateDetailForSelection's same-type
	// SetStructureData may rebind values onto the cached layout without re-running the entry
	// customization, leaving the build-time property filter stale -- ForceRefresh guarantees the
	// rebuild, matching the explicit ForceRefresh the tab detail views already get.
	if (StructDetailView.IsValid())
	{
		if (IDetailsView* Inner = StructDetailView->GetDetailsView())
		{
			Inner->ForceRefresh();
		}
	}
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
			if (!bCtrl)
			{
				SelectedIndices.Reset();
			}

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
	if (SelectedIndices.IsEmpty())
	{
		return FReply::Unhandled();
	}

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

	TSharedRef<FPCGExCollectionTileDragDropOp> DragOp =
		FPCGExCollectionTileDragDropOp::New(DraggedIndices, SourceCategory, Collection.Get());
	return FReply::Handled().BeginDragDrop(DragOp);
}

void SPCGExCollectionGridView::OnTileEntryChanged(int32 SourceIndex, FName PropertyName)
{
	PropagateTileProperty(SourceIndex, PropertyName);

	// Category changes grouping; defer one tick so we don't destroy the combobox during its
	// own selection-changed handler.
	static const FName CategoryPropName = GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category);
	if (PropertyName == CategoryPropName)
	{
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

	UpdateDetailForSelection();

	if (SelectedIndices.Num() > 1 && SelectedIndices.Contains(SourceIndex))
	{
		for (int32 Idx : SelectedIndices)
		{
			if (Idx == SourceIndex) { continue; }
			if (TSharedPtr<SPCGExCollectionGridTile> Tile = ActiveTiles.FindRef(Idx))
			{
				Tile->RefreshThumbnail();
			}
		}
	}
}

void SPCGExCollectionGridView::PropagateTileProperty(int32 SourceIndex, FName PropertyName)
{
	if (SelectedIndices.Num() < 2 || !SelectedIndices.Contains(SourceIndex))
	{
		return;
	}

	UScriptStruct* EntryStruct = GetEntryScriptStruct(SourceIndex);
	if (!EntryStruct) { return; }

	const FProperty* ChangedProp = EntryStruct->FindPropertyByName(PropertyName);
	if (!ChangedProp) { return; }

	const uint8* SrcPtr = GetEntryRawPtr(SourceIndex);
	if (!SrcPtr) { return; }

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	const int32 Offset = ChangedProp->GetOffset_ForInternal();
	const FStructProperty* StructProp = CastField<FStructProperty>(ChangedProp);

	// Suppress the deferred OnObjectModified refresh that PostEditChange below would otherwise schedule.
	TGuardValue<bool> SyncingGuard(bIsSyncing, true);

	Coll->Modify();

	for (int32 OtherIndex : SelectedIndices)
	{
		if (OtherIndex == SourceIndex) { continue; }
		uint8* OtherPtr = GetEntryRawPtr(OtherIndex);
		if (!OtherPtr) { continue; }

		// Mixed-type selections resolve the property on the target's own struct by name+type.
		const FProperty* DstProp = ResolveMatchingProperty(ChangedProp, EntryStruct, GetEntryScriptStruct(OtherIndex));
		if (!DstProp) { continue; }
		const int32 DstOffset = DstProp->GetOffset_ForInternal();

		// FInstancedStruct payload lives in an opaque heap buffer; descending via reflection finds no fields.
		if (StructProp && StructProp->Struct != TBaseStructure<FInstancedStruct>::Get())
		{
			PushStructGated(StructProp->Struct, SrcPtr + Offset, OtherPtr + DstOffset);
		}
		else
		{
			ChangedProp->CopyCompleteValue(OtherPtr + DstOffset, SrcPtr + Offset);
		}
	}

	// One PostEditChange sanitizes every modified entry -- EDITOR_RebuildStagingData iterates all of them.
	Coll->PostEditChange();
}

// ── Category operations ─────────────────────────────────────────────────────

void SPCGExCollectionGridView::OnTileDropOnCategory(FName TargetCategory, TSharedRef<FPCGExCollectionTileDragDropOp> DragOp, int32 InsertBeforeLocalIndex)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	const TArray<int32>& Indices = DragOp->DraggedIndices;
	if (!Coll || Indices.IsEmpty())
	{
		return;
	}

	// Cross-collection drop: indices reference a different collection's Entries array.
	if (DragOp->SourceCollection.Get() != Coll)
	{
		HandleCrossCollectionDrop(DragOp->SourceCollection.Get(), Indices, TargetCategory, InsertBeforeLocalIndex, DragOp->bIsCopyOperation);
		return;
	}

	FEntriesArrayAccess Access = GetEntriesAccess();
	UScriptStruct* EntryStruct = (Access.IsValid() && Access.InnerProp) ? Access.InnerProp->Struct : nullptr;

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Move Entries to Category"));
		Coll->Modify();

		// Step 1: Change categories
		for (int32 Index : Indices)
		{
			FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Index);
			if (Entry)
			{
				Entry->Category = TargetCategory;
			}
		}

		// InsertBeforeLocalIndex was computed against the ORIGINAL non-dragged tiles in the
		// target category, so no pre-removal adjustment is needed here.
		if (InsertBeforeLocalIndex != INDEX_NONE && EntryStruct && Access.IsValid())
		{
			RebuildCategoryCache();

			if (const TArray<int32>* CatIndices = CategoryToEntryIndices.Find(TargetCategory))
			{
				if (CatIndices->Num() >= 2)
				{
					const TSet<int32> DraggedSet(Indices);
					TArray<int32> DesiredOrder = ComputeCategoryDesiredOrder(*CatIndices, DraggedSet, InsertBeforeLocalIndex, /*bAdjustForDraggedBefore=*/false);
					if (!DesiredOrder.IsEmpty())
					{
						FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);
						TArray<int32> FinalPositions = ApplyCategoryPermutation(ArrayHelper, EntryStruct, *CatIndices, DesiredOrder, DraggedSet);
						if (!FinalPositions.IsEmpty())
						{
							SelectedIndices.Reset();
							for (int32 Idx : FinalPositions) { SelectedIndices.Add(Idx); }
							LastClickedIndex = FinalPositions[0];
						}
					}
				}
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	if (InsertBeforeLocalIndex == INDEX_NONE)
	{
		SelectedIndices.Reset();
		LastClickedIndex = INDEX_NONE;
	}

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::HandleCrossCollectionDrop(
	const UPCGExAssetCollection* SourceColl,
	const TArray<int32>& SourceIndices,
	FName TargetCategory,
	int32 InsertBeforeLocalIndex,
	bool bIsCopy)
{
	UPCGExAssetCollection* TargetColl = Collection.Get();
	if (!TargetColl || !SourceColl || SourceIndices.IsEmpty())
	{
		return;
	}

	FEntriesArrayAccess SourceAccess = GetEntriesAccess(SourceColl);
	FEntriesArrayAccess TargetAccess = GetEntriesAccess();
	if (!SourceAccess.IsValid() || !TargetAccess.IsValid())
	{
		return;
	}

	UScriptStruct* TargetElementStruct = TargetAccess.InnerProp ? TargetAccess.InnerProp->Struct : nullptr;
	if (!TargetElementStruct)
	{
		return;
	}

	FScriptArrayHelper SourceHelper(SourceAccess.ArrayProp, SourceAccess.ArrayData);
	const int32 SourceNum = SourceHelper.Num();

	TArray<int32> CleanIndices;
	CleanIndices.Reserve(SourceIndices.Num());
	for (int32 Idx : SourceIndices)
	{
		if (Idx >= 0 && Idx < SourceNum)
		{
			CleanIndices.Add(Idx);
		}
	}
	if (CleanIndices.IsEmpty())
	{
		return;
	}
	CleanIndices.Sort();

	// Snapshot source PAYLOADS (not raw elements) before any mutation -- a MOVE deletes
	// source rows. Per-row compatibility is arbitrated by EDITOR_AddEntry below.
	TArray<FInstancedStruct> Snapshots;
	TArray<int32> SnapshotSourceIndices;
	Snapshots.Reserve(CleanIndices.Num());
	SnapshotSourceIndices.Reserve(CleanIndices.Num());

	for (int32 Idx : CleanIndices)
	{
		const UScriptStruct* PayloadStruct = SourceColl->EDITOR_GetEntryScriptStruct(Idx);
		const FPCGExEntryAccessResult Row = SourceColl->GetEntryRaw(Idx);
		if (!PayloadStruct || !Row.IsValid())
		{
			continue; // Unset rows (heterogeneous hosts) carry nothing to transfer.
		}

		FInstancedStruct& Snapshot = Snapshots.Emplace_GetRef();
		Snapshot.InitializeAs(PayloadStruct, reinterpret_cast<const uint8*>(Row.Entry));
		SnapshotSourceIndices.Add(Idx);
	}

	if (Snapshots.IsEmpty())
	{
		return;
	}

	TArray<int32> InsertedIndices;
	TArray<int32> TransferredSourceIndices;
	TArray<int32> FinalDraggedPositions;
	int32 SkippedCount = 0;

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(bIsCopy
			                               ? INVTEXT("Copy Entries Across Collections")
			                               : INVTEXT("Move Entries Across Collections"));

		TargetColl->Modify();
		if (!bIsCopy)
		{
			const_cast<UPCGExAssetCollection*>(SourceColl)->Modify();
		}

		InsertedIndices.Reserve(Snapshots.Num());
		TransferredSourceIndices.Reserve(Snapshots.Num());

		for (int32 i = 0; i < Snapshots.Num(); ++i)
		{
			const UScriptStruct* PayloadStruct = Snapshots[i].GetScriptStruct();

			FPCGExAssetCollectionEntry* NewEntry = TargetColl->EDITOR_AddEntry(PayloadStruct);
			if (!NewEntry)
			{
				SkippedCount++;
				continue;
			}

			// Full copy when the target payload is the same type; base portion onto a native
			// element when a base-typed row lands in a typed collection.
			PayloadStruct->CopyScriptStruct(NewEntry, Snapshots[i].GetMemory());

			// The copy is SHALLOW for Instanced subobjects -- duplicate into the target.
			PCGExCollectionHelpers::DuplicateInstancedSubobjects(PayloadStruct, NewEntry, TargetColl);

			NewEntry->Category = TargetCategory;
			// NEW identity in the target: zero EntryId so SyncEntryIds re-mints it.
			NewEntry->EntryId = 0;

			InsertedIndices.Add(TargetColl->NumEntries() - 1);
			TransferredSourceIndices.Add(SnapshotSourceIndices[i]);
		}

		// Reverse iteration preserves earlier indices as later ones are removed. Only rows
		// that actually transferred leave the source.
		if (!bIsCopy && !TransferredSourceIndices.IsEmpty())
		{
			for (int32 i = TransferredSourceIndices.Num() - 1; i >= 0; --i)
			{
				SourceHelper.RemoveValues(TransferredSourceIndices[i], 1);
			}
			const_cast<UPCGExAssetCollection*>(SourceColl)->PostEditChange();
		}

		FinalDraggedPositions = InsertedIndices;

		if (InsertedIndices.IsEmpty())
		{
			// Every row was rejected: nothing changed (removal only runs for transferred
			// rows), so drop the transaction instead of leaving an empty undo step.
			Transaction.Cancel();
		}
		else
		{
			if (InsertBeforeLocalIndex != INDEX_NONE)
			{
				RebuildCategoryCache();
				if (const TArray<int32>* CatIndices = CategoryToEntryIndices.Find(TargetCategory))
				{
					const TSet<int32> DraggedSet(InsertedIndices);
					TArray<int32> DesiredOrder = ComputeCategoryDesiredOrder(*CatIndices, DraggedSet, InsertBeforeLocalIndex, /*bAdjustForDraggedBefore=*/false);
					if (!DesiredOrder.IsEmpty())
					{
						// Fresh helper AFTER the adds; permutation swaps whole ELEMENTS (payload-agnostic).
						FScriptArrayHelper TargetHelper(TargetAccess.ArrayProp, TargetAccess.ArrayData);
						TArray<int32> Reordered = ApplyCategoryPermutation(TargetHelper, TargetElementStruct, *CatIndices, DesiredOrder, DraggedSet);
						if (!Reordered.IsEmpty()) { FinalDraggedPositions = MoveTemp(Reordered); }
					}
				}
			}

			TargetColl->PostEditChange();

			SelectedIndices.Reset();
			for (int32 Idx : FinalDraggedPositions) { SelectedIndices.Add(Idx); }
			LastClickedIndex = FinalDraggedPositions.Num() > 0 ? FinalDraggedPositions[0] : INDEX_NONE;
		}
	}
	bIsBatchOperation = false;

	if (SkippedCount > 0)
	{
		FNotificationInfo Info(InsertedIndices.IsEmpty()
			                       ? FText(INVTEXT("Cannot drop entries: no entry is compatible with the target collection."))
			                       : FText::Format(INVTEXT("{0} incompatible entries were skipped."), SkippedCount));
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		Info.bFireAndForget = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(InsertedIndices.IsEmpty() ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}

		if (InsertedIndices.IsEmpty())
		{
			return;
		}
	}

	// Populate Staging.Path on new rows so thumbnails resolve immediately.
	TargetColl->EDITOR_RebuildStagingData();

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::OnAssetDropOnCategory(FName TargetCategory, const TArray<FAssetData>& Assets)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || Assets.IsEmpty())
	{
		return;
	}

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
				if (Entry)
				{
					Entry->Category = TargetCategory;
				}
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
	if (!Coll || OldName == NewName)
	{
		return;
	}

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
	// Same type-aware entry point as the main [+] -- a raw element add would create unset
	// (blank) rows on wrapper-row hosts.
	RequestAddEntry(Category);
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

void SPCGExCollectionGridView::OnTileReorderInCategory(FName Category, TSharedRef<FPCGExCollectionTileDragDropOp> DragOp, int32 InsertBeforeLocalIndex)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	const TArray<int32>& DraggedIndices = DragOp->DraggedIndices;
	if (!Coll)
	{
		return;
	}

	// Cross-collection drop that happened to land on a same-named category: route to
	// cross-collection handler instead of reordering this collection's entries.
	if (DragOp->SourceCollection.Get() != Coll)
	{
		HandleCrossCollectionDrop(DragOp->SourceCollection.Get(), DraggedIndices, Category, InsertBeforeLocalIndex, DragOp->bIsCopyOperation);
		return;
	}

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid())
	{
		return;
	}

	const TArray<int32>* CatIndices = CategoryToEntryIndices.Find(Category);
	if (!CatIndices || CatIndices->Num() < 2)
	{
		return;
	}

	UScriptStruct* EntryStruct = Access.InnerProp ? Access.InnerProp->Struct : nullptr;
	if (!EntryStruct)
	{
		return;
	}

	const TSet<int32> DraggedSet(DraggedIndices);
	TArray<int32> DesiredOrder = ComputeCategoryDesiredOrder(*CatIndices, DraggedSet, InsertBeforeLocalIndex, /*bAdjustForDraggedBefore=*/true);
	if (DesiredOrder.IsEmpty())
	{
		return;
	}

	FScriptArrayHelper ArrayHelper(Access.ArrayProp, Access.ArrayData);

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Reorder Entries"));
		Coll->Modify();

		TArray<int32> FinalPositions = ApplyCategoryPermutation(ArrayHelper, EntryStruct, *CatIndices, DesiredOrder, DraggedSet);

		Coll->PostEditChange();

		SelectedIndices.Reset();
		for (int32 Idx : FinalPositions) { SelectedIndices.Add(Idx); }
		LastClickedIndex = FinalPositions.Num() > 0 ? FinalPositions[0] : INDEX_NONE;
	}
	bIsBatchOperation = false;

	// Copy affected indices -- CatIndices pointer is invalidated by IncrementalCategoryRefresh
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
	if (!GroupPtr || !GroupPtr->IsValid())
	{
		return;
	}

	const TArray<int32>* Indices = CategoryToEntryIndices.Find(Category);
	if (!Indices || Indices->IsEmpty())
	{
		return;
	}

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
	if (bAlreadyPopulated)
	{
		return;
	}

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
			.BatchFlagPtr(&bIsBatchOperation)
			.OnTileClicked(FOnTileClicked::CreateSP(this, &SPCGExCollectionGridView::OnTileClicked))
			.OnTileDragDetected(FOnTileDragDetected::CreateSP(this, &SPCGExCollectionGridView::OnTileDragDetected))
			.OnTileEntryChanged(FOnTileEntryChanged::CreateSP(this, &SPCGExCollectionGridView::OnTileEntryChanged));

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
	// Force any focused detail-panel widget through its normal focus-lost path BEFORE
	// we swap the FStructOnScope under it. Without this:
	//   (a) Mid-edit values get dropped on selection change -- Slate destroying the
	//       focused widget as part of the detail-tree rebuild skips OnTextCommitted,
	//       so the IPropertyHandle->SetValue commit never runs.
	//   (b) Tearing down a focused widget whose commit lambdas reference the just-
	//       replaced FStructOnScope can hit dangling delegate instances during the
	//       in-flight rebuild, manifesting as DEP crashes inside Slate widget Construct
	//       (e.g. SNumericEntryBox lambda CreateCopy).
	// Re-focus the grid (not ClearKeyboardFocus) so the previously focused widget commits
	// any pending edit via OnFocusLost AND OnKeyDown (Delete, Ctrl+D) keeps working after.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Cleared);
	}

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

	UScriptStruct* EntryStruct = GetEntryScriptStruct(Index);
	uint8* EntryPtr = GetEntryRawPtr(Index);

	if (!EntryStruct || !EntryPtr)
	{
		// Unresolvable row (e.g. unset payload): clear the panel.
		CurrentStructScope.Reset();
		CurrentDetailIndex = INDEX_NONE;
		if (StructDetailView.IsValid())
		{
			StructDetailView->SetStructureData(nullptr);
		}
		return;
	}

	// Create FStructOnScope with a copy of the entry data
	CurrentStructScope = MakeShared<FStructOnScope>(EntryStruct);
	EntryStruct->CopyScriptStruct(CurrentStructScope->GetStructMemory(), EntryPtr);
	CurrentDetailIndex = Index;

	if (StructDetailView.IsValid())
	{
		StructDetailView->SetStructureData(CurrentStructScope);
	}
}

void SPCGExCollectionGridView::SyncStructToCollection(const FProperty* ChangedMemberProperty, const FProperty* ChangedLeafProperty)
{
	if (!CurrentStructScope.IsValid() || CurrentDetailIndex == INDEX_NONE)
	{
		return;
	}

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll)
	{
		return;
	}

	UScriptStruct* EntryStruct = GetEntryScriptStruct(CurrentDetailIndex);
	if (!EntryStruct)
	{
		return;
	}

	// The row's payload type may have changed since the scope was built (stale index across
	// remove/reorder/undo) -- copying across mismatched layouts corrupts memory.
	if (CurrentStructScope->GetStruct() != EntryStruct)
	{
		return;
	}

	uint8* PrimaryPtr = GetEntryRawPtr(CurrentDetailIndex);
	if (!PrimaryPtr)
	{
		return;
	}

	const uint8* SrcData = CurrentStructScope->GetStructMemory();

	Coll->Modify();

	// ── Step 1: Resolve which top-level member property changed ──────────
	// ChangedMemberProperty may be null or point to a property inside an external struct
	// (e.g. when PropertyOverrides values are edited through AddExternalStructureProperty),
	// so we verify it's a direct member of the entry struct first.
	const FProperty* PropToPropagate = nullptr;

	if (ChangedMemberProperty)
	{
		for (TFieldIterator<FProperty> It(EntryStruct); It; ++It)
		{
			if (*It == ChangedMemberProperty)
			{
				PropToPropagate = ChangedMemberProperty;
				break;
			}
		}
	}

	// If not a direct member (or null), diff the pre-copy primary data against the
	// edited source to find which top-level member actually changed.
	if (!PropToPropagate && SelectedIndices.Num() > 1)
	{
		for (TFieldIterator<FProperty> It(EntryStruct); It; ++It)
		{
			const FProperty* Prop = *It;
			const int32 Off = Prop->GetOffset_ForInternal();
			if (!Prop->Identical(PrimaryPtr + Off, SrcData + Off))
			{
				PropToPropagate = Prop;
				break;
			}
		}
	}

	// ── Step 2: Snapshot struct members before overwrite ──────────────────
	// When the changed member is a struct (e.g. Variations, PropertyOverrides),
	// snapshot its data from PrimaryPtr so we can diff after overwrite to
	// propagate only changed sub-properties / array elements.
	TArray<uint8> MemberSnapshot;
	const FStructProperty* StructMember = PropToPropagate
		? CastField<FStructProperty>(PropToPropagate)
		: nullptr;

	if (StructMember && SelectedIndices.Num() > 1)
	{
		const int32 MemberSize = StructMember->Struct->GetStructureSize();
		const int32 MemberOffset = PropToPropagate->GetOffset_ForInternal();
		MemberSnapshot.SetNumUninitialized(MemberSize);
		StructMember->Struct->InitializeStruct(MemberSnapshot.GetData());
		StructMember->Struct->CopyScriptStruct(MemberSnapshot.GetData(), PrimaryPtr + MemberOffset);
	}

	// ── Step 3: Copy entire struct back to the primary entry ─────────────
	EntryStruct->CopyScriptStruct(PrimaryPtr, SrcData);

	// ── Step 4: Propagate to other selected entries ──────────────────────
	// Mixed-type selections: targets resolve the member by name + type; incompatible skip.
	if (PropToPropagate && SelectedIndices.Num() > 1)
	{
		const int32 MemberOffset = PropToPropagate->GetOffset_ForInternal();

		if (StructMember && MemberSnapshot.Num() > 0)
		{
			// Granular: snapshot-diff propagation for struct members
			TArray<int32> Selected = GetSelectedIndices();
			for (int32 OtherIndex : Selected)
			{
				if (OtherIndex == CurrentDetailIndex)
				{
					continue;
				}
				uint8* OtherPtr = GetEntryRawPtr(OtherIndex);
				const FProperty* DstProp = ResolveMatchingProperty(PropToPropagate, EntryStruct, GetEntryScriptStruct(OtherIndex));
				if (OtherPtr && DstProp)
				{
					PropagateChangedProperties(
						MemberSnapshot.GetData(),
						SrcData + MemberOffset,
						OtherPtr + DstProp->GetOffset_ForInternal(),
						StructMember->Struct);
				}
			}

			StructMember->Struct->DestroyStruct(MemberSnapshot.GetData());
		}
		else
		{
			// Coarse: copy the entire top-level member
			TArray<int32> Selected = GetSelectedIndices();
			for (int32 OtherIndex : Selected)
			{
				if (OtherIndex == CurrentDetailIndex)
				{
					continue;
				}
				uint8* OtherPtr = GetEntryRawPtr(OtherIndex);
				const FProperty* DstProp = ResolveMatchingProperty(PropToPropagate, EntryStruct, GetEntryScriptStruct(OtherIndex));
				if (OtherPtr && DstProp)
				{
					PropToPropagate->CopyCompleteValue(OtherPtr + DstProp->GetOffset_ForInternal(), SrcData + MemberOffset);
				}
			}
		}
	}

	Coll->PostEditChange();
}

void SPCGExCollectionGridView::PropagateChangedProperties(
	const uint8* OldData, const uint8* NewData, uint8* DstData,
	const UStruct* Struct, bool bCheckEnabledGate)
{
	// When bCheckEnabledGate is true: if the destination element has bEnabled == false,
	// only propagate changes to bEnabled itself (skip Value and other fields).
	// This prevents override value edits from leaking into entries that have the override disabled.
	bool bGateBlocksNonGateFields = false;
	static const FName EnabledFieldName = TEXT("bEnabled");

	if (bCheckEnabledGate)
	{
		if (const FBoolProperty* EnabledProp = CastField<FBoolProperty>(Struct->FindPropertyByName(EnabledFieldName)))
		{
			bGateBlocksNonGateFields = !EnabledProp->GetPropertyValue(DstData + EnabledProp->GetOffset_ForInternal());
		}
	}

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Prop = *It;
		const int32 Offset = Prop->GetOffset_ForInternal();

		// Skip unchanged properties
		if (Prop->Identical(OldData + Offset, NewData + Offset))
		{
			continue;
		}

		// If gate is active, only allow the gate field (bEnabled) to propagate
		if (bGateBlocksNonGateFields && Prop->GetFName() != EnabledFieldName)
		{
			continue;
		}

		if (const FStructProperty* NestedStruct = CastField<FStructProperty>(Prop))
		{
			// FInstancedStruct stores polymorphic data in an internal heap buffer,
			// not as reflected UPROPERTYs. Recursing would find no meaningful fields
			// to propagate, so copy the complete value instead.
			if (NestedStruct->Struct == TBaseStructure<FInstancedStruct>::Get())
			{
				Prop->CopyCompleteValue(DstData + Offset, NewData + Offset);
			}
			else
			{
				// Recurse into nested struct for finer granularity
				PropagateChangedProperties(
					OldData + Offset, NewData + Offset, DstData + Offset,
					NestedStruct->Struct);
			}
		}
		else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			// Compare array elements individually
			FScriptArrayHelper OldHelper(ArrayProp, OldData + Offset);
			FScriptArrayHelper NewHelper(ArrayProp, NewData + Offset);
			FScriptArrayHelper DstHelper(ArrayProp, DstData + Offset);

			if (OldHelper.Num() != NewHelper.Num() || NewHelper.Num() != DstHelper.Num())
			{
				// Size mismatch -- must copy entire array
				Prop->CopyCompleteValue(DstData + Offset, NewData + Offset);
			}
			else
			{
				// Check if array elements are structs with a bEnabled gate
				const FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
				const bool bInnerHasEnabledGate = InnerStructProp
					&& InnerStructProp->Struct->FindPropertyByName(EnabledFieldName) != nullptr;

				const FProperty* Inner = ArrayProp->Inner;
				for (int32 i = 0; i < NewHelper.Num(); i++)
				{
					if (!Inner->Identical(OldHelper.GetRawPtr(i), NewHelper.GetRawPtr(i)))
					{
						if (InnerStructProp)
						{
							// Recurse into struct elements for field-level granularity + bEnabled gate
							PropagateChangedProperties(
								OldHelper.GetRawPtr(i), NewHelper.GetRawPtr(i), DstHelper.GetRawPtr(i),
								InnerStructProp->Struct, bInnerHasEnabledGate);
						}
						else
						{
							Inner->CopyCompleteValue(DstHelper.GetRawPtr(i), NewHelper.GetRawPtr(i));
						}
					}
				}
			}
		}
		else
		{
			// Leaf property -- copy directly
			Prop->CopyCompleteValue(DstData + Offset, NewData + Offset);
		}
	}
}

// ── Push helpers ────────────────────────────────────────────────────────────

void SPCGExCollectionGridView::PushStructGated(const UStruct* Struct, const uint8* Src, uint8* Dst)
{
	static const FName EnabledFieldName = TEXT("bEnabled");

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		const FProperty* Prop = *It;
		const int32 Offset = Prop->GetOffset_ForInternal();

		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			const FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
			const FBoolProperty* EnabledProp = InnerStructProp
				? CastField<FBoolProperty>(InnerStructProp->Struct->FindPropertyByName(EnabledFieldName))
				: nullptr;

			if (!EnabledProp)
			{
				Prop->CopyCompleteValue(Dst + Offset, Src + Offset);
				continue;
			}

			FScriptArrayHelper SrcHelper(ArrayProp, Src + Offset);
			FScriptArrayHelper DstHelper(ArrayProp, Dst + Offset);
			const int32 Num = SrcHelper.Num();

			// Differing sizes -- alignment isn't safe, fall back to full overwrite.
			// Same-size arrays under FPCGExPropertyOverrides::Overrides are guaranteed
			// parallel (same schema), so per-index gating is well-defined.
			if (Num != DstHelper.Num())
			{
				Prop->CopyCompleteValue(Dst + Offset, Src + Offset);
				continue;
			}

			const int32 EnabledOffset = EnabledProp->GetOffset_ForInternal();

			for (int32 i = 0; i < Num; ++i)
			{
				if (EnabledProp->GetPropertyValue(DstHelper.GetRawPtr(i) + EnabledOffset))
				{
					continue;
				}
				ArrayProp->Inner->CopyCompleteValue(DstHelper.GetRawPtr(i), SrcHelper.GetRawPtr(i));
			}
		}
		else if (const FStructProperty* NestedStruct = CastField<FStructProperty>(Prop))
		{
			// FInstancedStruct stores polymorphic data in an internal heap buffer,
			// not as reflected UPROPERTYs -- recursing would find no meaningful fields.
			if (NestedStruct->Struct == TBaseStructure<FInstancedStruct>::Get())
			{
				Prop->CopyCompleteValue(Dst + Offset, Src + Offset);
			}
			else
			{
				PushStructGated(NestedStruct->Struct, Src + Offset, Dst + Offset);
			}
		}
		else
		{
			Prop->CopyCompleteValue(Dst + Offset, Src + Offset);
		}
	}
}

void SPCGExCollectionGridView::ExecutePush(const PCGExAssetCollectionEditor::FPushOption& Option)
{
	if (Option.EntryPropertyNames.IsEmpty() || CurrentDetailIndex == INDEX_NONE)
	{
		return;
	}

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll)
	{
		return;
	}

	UScriptStruct* EntryStruct = GetEntryScriptStruct(CurrentDetailIndex);
	if (!EntryStruct)
	{
		return;
	}

	const uint8* SrcPtr = GetEntryRawPtr(CurrentDetailIndex);
	if (!SrcPtr)
	{
		return;
	}

	TArray<int32> Targets = GetSelectedIndices();
	Targets.Remove(CurrentDetailIndex);
	if (Targets.IsEmpty())
	{
		return;
	}

	// Resolve properties once -- struct doesn't change between iterations.
	// Missing names are silently skipped so cross-collection options can no-op gracefully.
	TArray<const FProperty*, TInlineAllocator<8>> ResolvedProps;
	ResolvedProps.Reserve(Option.EntryPropertyNames.Num());
	for (const FName& PropName : Option.EntryPropertyNames)
	{
		if (const FProperty* Prop = EntryStruct->FindPropertyByName(PropName))
		{
			ResolvedProps.Add(Prop);
		}
	}
	if (ResolvedProps.IsEmpty())
	{
		return;
	}

	// Guard suppresses the deferred OnObjectModified-driven refresh that would otherwise
	// fire from our own Modify() call. Mirrors OnDuplicateSelected / OnDeleteSelected.
	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(FText::Format(INVTEXT("Push {0} to selection"), Option.Label));
		Coll->Modify();

		for (int32 TargetIdx : Targets)
		{
			uint8* TgtPtr = GetEntryRawPtr(TargetIdx);
			if (!TgtPtr)
			{
				continue;
			}

			// Mixed-type selections: resolve per property by name + type; incompatible skip.
			const UScriptStruct* TargetStruct = GetEntryScriptStruct(TargetIdx);

			for (const FProperty* Prop : ResolvedProps)
			{
				const FProperty* DstProp = ResolveMatchingProperty(Prop, EntryStruct, TargetStruct);
				if (!DstProp)
				{
					continue;
				}

				const int32 Offset = Prop->GetOffset_ForInternal();
				const int32 DstOffset = DstProp->GetOffset_ForInternal();

				// Gated path only descends into reflected structs. Everything else
				// (non-struct members, FInstancedStruct opaque buffers) falls back to a
				// straight overwrite since there's no inner bEnabled to gate on.
				const FStructProperty* StructProp = Option.bRespectEnabledGate
					? CastField<FStructProperty>(Prop)
					: nullptr;

				if (StructProp && StructProp->Struct != TBaseStructure<FInstancedStruct>::Get())
				{
					PushStructGated(StructProp->Struct, SrcPtr + Offset, TgtPtr + DstOffset);
				}
				else
				{
					Prop->CopyCompleteValue(TgtPtr + DstOffset, SrcPtr + Offset);
				}
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	// Refresh AFTER the transaction commits so we don't interleave widget updates with the
	// transaction's serialize-on-close pass.
	for (int32 TargetIdx : Targets)
	{
		if (TSharedPtr<SPCGExCollectionGridTile> Tile = ActiveTiles.FindRef(TargetIdx))
		{
			Tile->RefreshThumbnail();
		}
	}

	// Deliberately do NOT call UpdateDetailForSelection() here. The active entry (source)
	// is the one shown in the detail panel and its data is unchanged by the push, so the
	// existing CurrentStructScope is still accurate. Rebuilding it would swap the
	// FStructOnScope under FPCGExPropertyOverrideEntryCustomization::InnerScope -- a
	// non-owning scope that aliases the outer scope's FInstancedStruct heap memory -- and
	// strand the customization's widgets on freed memory (manifests as edits reverting and
	// a paint-time crash inside MakeSoftClassPathWidget on the next detail repaint).
}

void SPCGExCollectionGridView::OnDetailPropertyChanged(const FPropertyChangedEvent& Event)
{
	bIsSyncing = true;
	SyncStructToCollection(Event.MemberProperty, Event.Property);
	bIsSyncing = false;

	// Check if category changed -- need to rebuild groups
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

// ── Entry payload access helpers ────────────────────────────────────────────

UScriptStruct* SPCGExCollectionGridView::GetEntryScriptStruct(int32 Index) const
{
	const UPCGExAssetCollection* Coll = Collection.Get();
	return Coll ? const_cast<UScriptStruct*>(Coll->EDITOR_GetEntryScriptStruct(Index)) : nullptr;
}

uint8* SPCGExCollectionGridView::GetEntryRawPtr(int32 Index) const
{
	// Payload base pointer via collection virtuals (== raw element on homogeneous hosts;
	// null when a wrapper row's payload is unset).
	UPCGExAssetCollection* Coll = Collection.Get();
	return Coll ? reinterpret_cast<uint8*>(Coll->GetMutableEntryRaw(Index)) : nullptr;
}

const FProperty* SPCGExCollectionGridView::ResolveMatchingProperty(const FProperty* SrcProp, const UScriptStruct* SrcStruct, const UScriptStruct* DstStruct)
{
	if (!SrcProp || !DstStruct)
	{
		return nullptr;
	}

	if (DstStruct == SrcStruct)
	{
		return SrcProp;
	}

	const FProperty* DstProp = DstStruct->FindPropertyByName(SrcProp->GetFName());
	return (DstProp && DstProp->SameType(SrcProp)) ? DstProp : nullptr;
}

TArray<int32> SPCGExCollectionGridView::ComputeCategoryDesiredOrder(
	const TArray<int32>& CatIndices,
	const TSet<int32>& DraggedSet,
	int32 InsertBeforeLocalIndex,
	bool bAdjustForDraggedBefore)
{
	TArray<int32> Dragged;
	TArray<int32> NonDragged;
	NonDragged.Reserve(CatIndices.Num());
	Dragged.Reserve(DraggedSet.Num());

	for (int32 Idx : CatIndices)
	{
		if (DraggedSet.Contains(Idx)) { Dragged.Add(Idx); }
		else { NonDragged.Add(Idx); }
	}

	if (Dragged.IsEmpty()) { return {}; }

	int32 AdjustedInsert = InsertBeforeLocalIndex;
	if (bAdjustForDraggedBefore)
	{
		for (int32 i = 0; i < InsertBeforeLocalIndex && i < CatIndices.Num(); i++)
		{
			if (DraggedSet.Contains(CatIndices[i])) { AdjustedInsert--; }
		}
	}
	AdjustedInsert = FMath::Clamp(AdjustedInsert, 0, NonDragged.Num());

	TArray<int32> DesiredOrder;
	DesiredOrder.Reserve(CatIndices.Num());
	for (int32 i = 0; i < AdjustedInsert; i++) { DesiredOrder.Add(NonDragged[i]); }
	DesiredOrder.Append(Dragged);
	for (int32 i = AdjustedInsert; i < NonDragged.Num(); i++) { DesiredOrder.Add(NonDragged[i]); }

	if (DesiredOrder == CatIndices) { return {}; }

	return DesiredOrder;
}

TArray<int32> SPCGExCollectionGridView::ApplyCategoryPermutation(
	FScriptArrayHelper& Helper,
	UScriptStruct* Struct,
	const TArray<int32>& CatIndices,
	const TArray<int32>& DesiredOrder,
	const TSet<int32>& DraggedSet)
{
	const int32 StructSize = Struct->GetStructureSize();
	TArray<uint8> Temp;
	Temp.SetNumUninitialized(StructSize);
	Struct->InitializeStruct(Temp.GetData());

	TArray<int32> CurrentOrder = CatIndices;

	// Position map keeps swap lookup O(1) so the overall permutation is O(N) rather than O(N²)
	// -- matters once a category holds hundreds of entries.
	TMap<int32, int32> Pos;
	Pos.Reserve(CurrentOrder.Num());
	for (int32 k = 0; k < CurrentOrder.Num(); k++) { Pos.Add(CurrentOrder[k], k); }

	for (int32 i = 0; i < DesiredOrder.Num(); i++)
	{
		if (CurrentOrder[i] == DesiredOrder[i]) { continue; }

		const int32* JPtr = Pos.Find(DesiredOrder[i]);
		check(JPtr);
		const int32 j = *JPtr;

		uint8* PtrA = Helper.GetRawPtr(CatIndices[i]);
		uint8* PtrB = Helper.GetRawPtr(CatIndices[j]);

		Struct->CopyScriptStruct(Temp.GetData(), PtrA);
		Struct->CopyScriptStruct(PtrA, PtrB);
		Struct->CopyScriptStruct(PtrB, Temp.GetData());

		Pos[CurrentOrder[i]] = j;
		Pos[CurrentOrder[j]] = i;
		Swap(CurrentOrder[i], CurrentOrder[j]);
	}

	Struct->DestroyStruct(Temp.GetData());

	TArray<int32> FinalDraggedPositions;
	FinalDraggedPositions.Reserve(DraggedSet.Num());
	for (int32 i = 0; i < CurrentOrder.Num(); i++)
	{
		if (DraggedSet.Contains(CurrentOrder[i]))
		{
			FinalDraggedPositions.Add(CatIndices[i]);
		}
	}
	return FinalDraggedPositions;
}

SPCGExCollectionGridView::FEntriesArrayAccess SPCGExCollectionGridView::GetEntriesAccess(const UPCGExAssetCollection* InColl) const
{
	FEntriesArrayAccess Result;

	const UPCGExAssetCollection* Coll = InColl ? InColl : Collection.Get();
	if (!Coll)
	{
		return Result;
	}

	Result.ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(PCGExAssetCollectionEditor::EntriesName));
	if (!Result.ArrayProp)
	{
		return Result;
	}

	Result.InnerProp = CastField<FStructProperty>(Result.ArrayProp->Inner);
	Result.ArrayData = Result.ArrayProp->ContainerPtrToValuePtr<void>(const_cast<UPCGExAssetCollection*>(Coll));

	return Result;
}

// ── Entry operations ────────────────────────────────────────────────────────

FReply SPCGExCollectionGridView::OnAddEntry()
{
	RequestAddEntry(NAME_None);
	return FReply::Handled();
}

void SPCGExCollectionGridView::RequestAddEntry(FName Category)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll)
	{
		return;
	}

	// Heterogeneous hosts require an explicit type choice; homogeneous take the untyped add.
	TArray<const UScriptStruct*> AddableTypes;
	Coll->EDITOR_GetAddableEntryTypes(AddableTypes);

	if (AddableTypes.IsEmpty())
	{
		AddEntryOfStruct(nullptr, Category);
		return;
	}

	// Labels prefer the ShortName meta ("Actor") over the full display name.
	AddableTypes.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return PCGExCollectionEditorUtils::GetEntryTypeLabel(&A).CompareTo(PCGExCollectionEditorUtils::GetEntryTypeLabel(&B)) < 0;
	});

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	MenuBuilder.BeginSection(NAME_None, INVTEXT("Add Entry"));
	for (const UScriptStruct* EntryStruct : AddableTypes)
	{
		const FText Label = PCGExCollectionEditorUtils::GetEntryTypeLabel(EntryStruct);
		MenuBuilder.AddMenuEntry(
			Label,
			FText::Format(INVTEXT("Add a new {0} entry to the collection."), Label),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSPLambda(this, [this, EntryStruct, Category]()
			{
				AddEntryOfStruct(EntryStruct, Category);
			})));
	}
	MenuBuilder.EndSection();

	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		MenuBuilder.MakeWidget(),
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

void SPCGExCollectionGridView::AddEntryOfStruct(const UScriptStruct* EntryStruct, FName Category)
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll)
	{
		return;
	}

	bool bAdded = false;

	bIsBatchOperation = true;
	{
		FScopedTransaction Transaction(INVTEXT("Add Collection Entry"));

		// Suppress staging rebuild -- nothing to stage on an empty entry
		Coll->bSuppressStagingRebuild = true;

		Coll->Modify();

		FPCGExAssetCollectionEntry* NewEntry = Coll->EDITOR_AddEntry(EntryStruct);
		const int32 NewIndex = Coll->NumEntries() - 1;

		Coll->bSuppressStagingRebuild = false;

		if (NewEntry && !Category.IsNone())
		{
			NewEntry->Category = Category;
		}

		Coll->PostEditChange();

		// Sync PropertyOverrides for the new entry to match the collection schema
		Coll->SyncPropertyOverridesToEntries();

		// Select the new entry
		if (NewEntry)
		{
			bAdded = true;
			SelectedIndices.Reset();
			SelectedIndices.Add(NewIndex);
			LastClickedIndex = NewIndex;
		}
	}
	bIsBatchOperation = false;

	IncrementalCategoryRefresh();
	UpdateDetailForSelection();

	// Uncategorized adds land at the array tail; category adds land inside their group.
	if (bAdded && Category.IsNone() && GroupScrollBox.IsValid())
	{
		GroupScrollBox->ScrollToEnd();
	}
}

FReply SPCGExCollectionGridView::OnDuplicateSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll)
	{
		return FReply::Handled();
	}

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid())
	{
		return FReply::Handled();
	}

	UScriptStruct* EntryStruct = Access.InnerProp ? Access.InnerProp->Struct : nullptr;
	if (!EntryStruct)
	{
		return FReply::Handled();
	}

	TArray<int32> Selected = SelectedIndices.Array();
	Selected.Sort();
	if (Selected.IsEmpty())
	{
		return FReply::Handled();
	}

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

			// A duplicate is a NEW identity: zero EntryId so the ORIGINAL keeps its id. Via
			// collection virtuals -- DstPtr is the raw ELEMENT; casting it would corrupt
			// wrapper rows.
			if (FPCGExAssetCollectionEntry* DuplicatePayload = Coll->GetMutableEntryRaw(InsertAt))
			{
				DuplicatePayload->EntryId = 0;
			}
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
			if (LastClickedIndex == INDEX_NONE)
			{
				LastClickedIndex = FinalPos;
			}
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
	if (!Coll)
	{
		return FReply::Handled();
	}

	FEntriesArrayAccess Access = GetEntriesAccess();
	if (!Access.IsValid())
	{
		return FReply::Handled();
	}

	TArray<int32> Selected = SelectedIndices.Array();
	Selected.Sort();
	if (Selected.IsEmpty())
	{
		return FReply::Handled();
	}

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
		TileOp->bIsCopyOperation = InDragDropEvent.IsAltDown();
		OnTileDropOnCategory(NAME_None, TileOp.ToSharedRef(), INDEX_NONE);
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
	if (Object != Collection.Get())
	{
		return;
	}
	if (bIsSyncing || bIsBatchOperation)
	{
		return;
	}
	if (bPendingExternalRefresh)
	{
		return;
	} // Already scheduled

	// Defer to next tick -- Modify() fires BEFORE changes are applied,
	// so the entry count / data hasn't been updated yet.
	bPendingExternalRefresh = true;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		                    [this](double, float) -> EActiveTimerReturnType
		                    {
			                    bPendingExternalRefresh = false;

			                    if (bIsSyncing || bIsBatchOperation)
			                    {
				                    return EActiveTimerReturnType::Stop;
			                    }

			                    const int32 CurrentCount = Collection.IsValid() ? Collection->NumEntries() : 0;
			                    if (CurrentCount != VisualOrder.Num())
			                    {
				                    // Entry count changed externally -- rebuild staging for new entries
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
