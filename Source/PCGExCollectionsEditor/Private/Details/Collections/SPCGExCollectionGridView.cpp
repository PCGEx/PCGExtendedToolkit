// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridView.h"

#include "AssetThumbnail.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExAssetCollectionEditor.h"
#include "Details/Collections/SPCGExCollectionGridTile.h"
#include "Modules/ModuleManager.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#pragma region SPCGExCollectionGridView

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
	EntryItems.Empty();

	if (const UPCGExAssetCollection* Coll = Collection.Get())
	{
		const int32 Num = Coll->NumEntries();
		EntryItems.Reserve(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			EntryItems.Add(MakeShared<int32>(i));
		}
	}
}

void SPCGExCollectionGridView::RefreshGrid()
{
	RebuildEntryItems();
	if (TileView.IsValid())
	{
		TileView->RequestListRefresh();
	}

	// Re-init row generator so entry operation handles are up-to-date
	InitRowGenerator();

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

	return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
		.Padding(4.f)
		[
			SNew(SPCGExCollectionGridTile)
			.EntryHandle(EntryHandle)
			.ThumbnailPool(ThumbnailPool)
			.OnGetPickerWidget(OnGetPickerWidget)
			.TileSize(TileSize)
			.Collection(Collection)
			.EntryIndex(Index)
		];
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

void SPCGExCollectionGridView::SyncStructToCollection()
{
	if (!CurrentStructScope.IsValid() || CurrentDetailIndex == INDEX_NONE) { return; }

	UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll) { return; }

	UScriptStruct* EntryStruct = GetEntryScriptStruct();
	if (!EntryStruct) { return; }

	// Copy modified data back to the primary selected entry
	uint8* EntryPtr = GetEntryRawPtr(CurrentDetailIndex);
	if (!EntryPtr) { return; }

	Coll->Modify();
	EntryStruct->CopyScriptStruct(EntryPtr, CurrentStructScope->GetStructMemory());

	// Propagate to all other selected entries (multi-select editing)
	TArray<int32> Selected = GetSelectedIndices();
	for (int32 OtherIndex : Selected)
	{
		if (OtherIndex == CurrentDetailIndex) { continue; }
		uint8* OtherPtr = GetEntryRawPtr(OtherIndex);
		if (OtherPtr)
		{
			EntryStruct->CopyScriptStruct(OtherPtr, CurrentStructScope->GetStructMemory());
		}
	}

	Coll->PostEditChange();
}

void SPCGExCollectionGridView::OnDetailPropertyChanged(const FPropertyChangedEvent& Event)
{
	bIsSyncing = true;
	SyncStructToCollection();
	bIsSyncing = false;

	// Refresh tiles to reflect updated data
	if (TileView.IsValid())
	{
		TileView->RebuildList();
	}
}

void SPCGExCollectionGridView::OnRowGeneratorPropertyChanged(const FPropertyChangedEvent& Event)
{
	// Skip if we're already syncing from the detail panel to avoid redundant work
	if (bIsSyncing) { return; }

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
	if (!EntriesArrayHandle.IsValid()) { return FReply::Handled(); }

	TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
	if (ArrayHandle.IsValid())
	{
		ArrayHandle->AddItem();
		RefreshGrid();
	}

	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDuplicateSelected()
{
	if (!EntriesArrayHandle.IsValid()) { return FReply::Handled(); }

	TArray<int32> Selected = GetSelectedIndices();
	if (Selected.IsEmpty()) { return FReply::Handled(); }

	TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
	if (!ArrayHandle.IsValid()) { return FReply::Handled(); }

	// Duplicate in reverse order to preserve indices
	Selected.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Index : Selected)
	{
		ArrayHandle->DuplicateItem(Index);
	}

	RefreshGrid();
	return FReply::Handled();
}

FReply SPCGExCollectionGridView::OnDeleteSelected()
{
	if (!EntriesArrayHandle.IsValid()) { return FReply::Handled(); }

	TArray<int32> Selected = GetSelectedIndices();
	if (Selected.IsEmpty()) { return FReply::Handled(); }

	TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
	if (!ArrayHandle.IsValid()) { return FReply::Handled(); }

	// Delete in reverse order to preserve indices
	Selected.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Index : Selected)
	{
		ArrayHandle->DeleteItem(Index);
	}

	RefreshGrid();
	return FReply::Handled();
}

#pragma endregion
