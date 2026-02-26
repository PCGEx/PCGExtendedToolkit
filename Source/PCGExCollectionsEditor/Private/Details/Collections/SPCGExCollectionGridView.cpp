// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridView.h"

#include "AssetThumbnail.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

#include "InputCoreTypes.h"
#include "ScopedTransaction.h"

#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Details/Collections/SPCGExCollectionGridTile.h"
#include "Misc/TransactionObjectEvent.h"
#include "Modules/ModuleManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#pragma region SPCGExCollectionGridView

FReply SPCGExCollectionGridView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		if (TileView.IsValid() && TileView->GetNumItemsSelected() > 0)
		{
			return OnDeleteSelected();
		}
	}

	if (InKeyEvent.GetKey() == EKeys::D && InKeyEvent.IsControlDown())
	{
		if (TileView.IsValid() && TileView->GetNumItemsSelected() > 0)
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

	RebuildEntryItems();
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

	// Wire up property change callback to sync edits back to the collection
	StructDetailView->GetOnFinishedChangingPropertiesDelegate().AddSP(
		this, &SPCGExCollectionGridView::OnDetailPropertyChanged);

	// Listen for undo/redo to fully refresh the grid when the collection is restored
	FCoreUObjectDelegates::OnObjectTransacted.AddSP(
		this, &SPCGExCollectionGridView::OnObjectTransacted);

	const float TileWidgetSize = TileSize + 24.f;

	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		.PhysicalSplitterHandleSize(4.f)

		// Left pane: Tile grid
		+ SSplitter::Slot()
		.Value(0.65f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(4.f)
			[
				SAssignNew(TileView, STileView<TSharedPtr<int32>>)
				.ListItemsSource(&EntryItems)
				.OnGenerateTile(this, &SPCGExCollectionGridView::OnGenerateTile)
				.OnSelectionChanged(this, &SPCGExCollectionGridView::OnSelectionChanged)
				.SelectionMode(ESelectionMode::Multi)
				.ItemWidth(TileWidgetSize)
				.ItemHeight(TileWidgetSize + 80.f)
				.ItemAlignment(EListItemAlignment::LeftAligned)
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
					.Text(INVTEXT("Add"))
					.ToolTipText(INVTEXT("Add a new default entry to the collection"))
					.OnClicked(this, &SPCGExCollectionGridView::OnAddEntry)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(INVTEXT("Duplicate"))
					.ToolTipText(INVTEXT("Duplicate the selected entries"))
					.OnClicked(this, &SPCGExCollectionGridView::OnDuplicateSelected)
					.IsEnabled_Lambda([this]() { return TileView.IsValid() && TileView->GetNumItemsSelected() > 0; })
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(INVTEXT("Delete"))
					.ToolTipText(INVTEXT("Delete the selected entries"))
					.OnClicked(this, &SPCGExCollectionGridView::OnDeleteSelected)
					.IsEnabled_Lambda([this]() { return TileView.IsValid() && TileView->GetNumItemsSelected() > 0; })
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
}

void SPCGExCollectionGridView::RebuildEntryItems()
{
	const int32 Num = Collection.IsValid() ? Collection->NumEntries() : 0;

	// Always create fresh item pointers so STileView regenerates tile widgets.
	// Tiles hold IPropertyHandles and cached state from construction —
	// reusing widgets after array mutations would show stale data.
	EntryItems.Reset(Num);
	ActiveTiles.Reset();
	for (int32 i = 0; i < Num; ++i)
	{
		EntryItems.Add(MakeShared<int32>(i));
	}
}

void SPCGExCollectionGridView::RefreshGrid()
{
	RebuildEntryItems();
	if (TileView.IsValid())
	{
		TileView->RequestListRefresh();
	}

	// Clear detail panel selection
	CurrentDetailIndex = INDEX_NONE;
	CurrentStructScope.Reset();
	if (StructDetailView.IsValid())
	{
		TSharedPtr<FStructOnScope> NullStruct;
		StructDetailView->SetStructureData(NullStruct);
	}
}

void SPCGExCollectionGridView::RefreshDetailPanel()
{
	UpdateDetailForSelection();
}

TArray<int32> SPCGExCollectionGridView::GetSelectedIndices() const
{
	TArray<int32> Result;
	if (TileView.IsValid())
	{
		TArray<TSharedPtr<int32>> SelectedItems = TileView->GetSelectedItems();
		for (const TSharedPtr<int32>& Item : SelectedItems)
		{
			if (Item.IsValid()) { Result.Add(*Item); }
		}
	}
	return Result;
}

TSharedRef<ITableRow> SPCGExCollectionGridView::OnGenerateTile(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const int32 Index = Item.IsValid() ? *Item : INDEX_NONE;

	// Get the entry property handle from the array
	TSharedPtr<IPropertyHandle> EntryHandle;
	if (EntriesArrayHandle.IsValid() && Index != INDEX_NONE)
	{
		TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
		if (ArrayHandle.IsValid())
		{
			uint32 NumElements = 0;
			ArrayHandle->GetNumElements(NumElements);
			if (static_cast<uint32>(Index) < NumElements)
			{
				EntryHandle = ArrayHandle->GetElement(Index);
			}
		}
	}

	if (!EntryHandle.IsValid() || !EntryHandle->GetProperty())
	{
		return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
		[
			SNew(SBox)
			.WidthOverride(TileSize)
			.HeightOverride(TileSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(
					FText::Format(INVTEXT("[{0}] No handle (Array:{1})"),
						FText::AsNumber(Index),
						EntriesArrayHandle.IsValid() ? INVTEXT("Y") : INVTEXT("N")))
			]
		];
	}

	TSharedPtr<SPCGExCollectionGridTile> Tile;

	TSharedRef<STableRow<TSharedPtr<int32>>> Row = SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
		.Padding(4.f)
		[
			SAssignNew(Tile, SPCGExCollectionGridTile)
			.EntryHandle(EntryHandle)
			.ThumbnailPool(ThumbnailPool)
			.OnGetPickerWidget(OnGetPickerWidget)
			.TileSize(TileSize)
			.Collection(Collection)
			.EntryIndex(Index)
		];

	ActiveTiles.Add(Index, Tile);
	return Row;
}

void SPCGExCollectionGridView::OnSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
	UpdateDetailForSelection();
}

void SPCGExCollectionGridView::UpdateDetailForSelection()
{
	TArray<int32> Selected = GetSelectedIndices();

	if (Selected.IsEmpty())
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

	// Show the first selected entry
	const int32 Index = Selected[0];
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

	// Refresh only the selected tile(s) thumbnails — property value widgets
	// on tiles auto-update via their live IPropertyHandle bindings.
	TArray<int32> Selected = GetSelectedIndices();
	for (int32 Index : Selected)
	{
		if (TSharedPtr<SPCGExCollectionGridTile> Tile = ActiveTiles.FindRef(Index).Pin())
		{
			Tile->RefreshThumbnail();
		}
	}
}

void SPCGExCollectionGridView::OnRowGeneratorPropertyChanged(const FPropertyChangedEvent& Event)
{
	// Skip during batch operations or detail panel sync
	if (bIsSyncing || bIsBatchOperation) { return; }

	// A tile control changed — re-sync the detail panel from collection data
	UpdateDetailForSelection();
}

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

void SPCGExCollectionGridView::OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
{
	if (Object == Collection.Get() && Event.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		// Undo/redo replaces the object's internal state — RowGenerator handles become stale
		InitRowGenerator();
		RefreshGrid();
	}
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

FReply SPCGExCollectionGridView::OnAddEntry()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Coll->GetClass()->FindPropertyByName(FName("Entries")));
	if (!ArrayProp) { return FReply::Handled(); }

	// Direct array manipulation — bypasses per-item PostEditChangeProperty which would
	// trigger EDITOR_RebuildStagingData (synchronously loading ALL entry assets).
	// For Add, we also suppress staging entirely since the new entry is empty — staging
	// will rebuild naturally when the user picks an asset (via tile picker PostEditChangeProperty).
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

	// Re-find the Entries handle from the existing RowGenerator (PostEditChange
	// causes it to refresh its tree internally). Avoids the cost of InitRowGenerator
	// which creates an entirely new RowGenerator + SetObjects (full property tree build).
	EntriesArrayHandle.Reset();
	if (RowGenerator.IsValid())
	{
		const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = RowGenerator->GetRootTreeNodes();
		FindEntriesHandleRecursive(RootNodes, EntriesArrayHandle);
	}

	RefreshGrid();

	// Select the newly added entry
	if (!EntryItems.IsEmpty() && TileView.IsValid())
	{
		TileView->SetSelection(EntryItems.Last());
		TileView->RequestScrollIntoView(EntryItems.Last());
	}

	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDuplicateSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	TArray<int32> Selected = GetSelectedIndices();
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
		Selected.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 Index : Selected)
		{
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

	// Re-find the Entries handle from the existing RowGenerator
	EntriesArrayHandle.Reset();
	if (RowGenerator.IsValid())
	{
		const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = RowGenerator->GetRootTreeNodes();
		FindEntriesHandleRecursive(RootNodes, EntriesArrayHandle);
	}

	RefreshGrid();
	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDeleteSelected()
{
	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return FReply::Handled(); }

	TArray<int32> Selected = GetSelectedIndices();
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
		Selected.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 Index : Selected)
		{
			if (Index < ArrayHelper.Num())
			{
				ArrayHelper.RemoveValues(Index, 1);
			}
		}

		Coll->PostEditChange();
	}
	bIsBatchOperation = false;

	// Re-find the Entries handle from the existing RowGenerator
	EntriesArrayHandle.Reset();
	if (RowGenerator.IsValid())
	{
		const TArray<TSharedRef<IDetailTreeNode>>& RootNodes = RowGenerator->GetRootTreeNodes();
		FindEntriesHandleRecursive(RootNodes, EntriesArrayHandle);
	}

	RefreshGrid();
	return FReply::Handled();
}

#pragma endregion
