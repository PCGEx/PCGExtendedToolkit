// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridView.h"

#include "AssetThumbnail.h"
#include "DetailLayoutBuilder.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "PCGExCollectionsEditorSettings.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/SPCGExCollectionGridTile.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SPCGExCollectionGridView::Construct(const FArguments& InArgs)
{
	Collection = InArgs._Collection;
	ThumbnailPool = InArgs._ThumbnailPool;
	OnGetPickerWidget = InArgs._OnGetPickerWidget;
	TileSize = InArgs._TileSize;
	TilePropertyNames = InArgs._TilePropertyNames;

	RebuildEntryItems();
	InitRowGenerator();

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

			// Action buttons
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

			// Detail content
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(4, 0, 4, 4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(4.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(DetailPanelBox, SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						.Padding(0, 20)
						[
							SNew(STextBlock)
							.Text(INVTEXT("Select an entry to view details"))
							.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.4f)))
						]
					]
				]
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
	// Re-init row generator so handles are up-to-date
	InitRowGenerator();
	// Clear detail panel
	if (DetailPanelBox.IsValid())
	{
		DetailPanelBox->ClearChildren();
		DetailPanelBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0, 20)
			[
				SNew(STextBlock)
				.Text(INVTEXT("Select an entry to view details"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.4f)))
			];
	}
}

void SPCGExCollectionGridView::RefreshDetailPanel()
{
	TArray<int32> Selected = GetSelectedIndices();
	if (!Selected.IsEmpty())
	{
		PopulateDetailPanel(Selected);
	}
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
		];
}

void SPCGExCollectionGridView::OnSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
	TArray<int32> Selected = GetSelectedIndices();
	PopulateDetailPanel(Selected);
}

void SPCGExCollectionGridView::PopulateDetailPanel(const TArray<int32>& SelectedIndices)
{
	if (!DetailPanelBox.IsValid()) { return; }
	DetailPanelBox->ClearChildren();

	if (SelectedIndices.IsEmpty())
	{
		DetailPanelBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0, 20)
			[
				SNew(STextBlock)
				.Text(INVTEXT("Select an entry to view details"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.4f)))
			];
		return;
	}

	// Multi-selection indicator
	if (SelectedIndices.Num() > 1)
	{
		DetailPanelBox->AddSlot()
			.AutoHeight()
			.Padding(0, 4)
			[
				SNew(STextBlock)
				.Text(FText::Format(INVTEXT("Editing {0} entries"), FText::AsNumber(SelectedIndices.Num())))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.8f, 1.f)))
			];
	}

	if (!EntriesArrayHandle.IsValid()) { return; }

	// Show properties for the first selected entry
	const int32 PrimaryIndex = SelectedIndices[0];
	TSharedPtr<IPropertyHandle> PrimaryEntryHandle;
	{
		TSharedPtr<IPropertyHandleArray> ArrayHandle = EntriesArrayHandle->AsArray();
		if (ArrayHandle.IsValid())
		{
			PrimaryEntryHandle = ArrayHandle->GetElement(PrimaryIndex);
		}
	}
	if (!PrimaryEntryHandle.IsValid()) { return; }

	// Entry index label
	DetailPanelBox->AddSlot()
		.AutoHeight()
		.Padding(0, 2, 0, 4)
		[
			SNew(STextBlock)
			.Text(FText::Format(INVTEXT("Entry [{0}]"), FText::AsNumber(PrimaryIndex)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		];

	// Iterate children and add property widgets, skipping tile properties
	uint32 NumChildren = 0;
	PrimaryEntryHandle->GetNumChildren(NumChildren);

	const UPCGExCollectionsEditorSettings* Settings = GetDefault<UPCGExCollectionsEditorSettings>();

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = PrimaryEntryHandle->GetChildHandle(i);
		if (!ChildHandle.IsValid()) { continue; }

		const FName ChildName = ChildHandle->GetProperty() ? ChildHandle->GetProperty()->GetFName() : NAME_None;

		// Skip properties already shown on the tile
		if (TilePropertyNames.Contains(ChildName)) { continue; }

		// Apply filter visibility from settings
		const EVisibility Vis = Settings->GetPropertyVisibility(ChildName);
		if (Vis == EVisibility::Collapsed) { continue; }

		DetailPanelBox->AddSlot()
			.AutoHeight()
			.Padding(0, 1)
			[
				SNew(SHorizontalBox)

				// Property name
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SBox)
					.MinDesiredWidth(100)
					[
						SNew(STextBlock)
						.Text(ChildHandle->GetPropertyDisplayName())
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.7f)))
					]
				]

				// Property value widget
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					ChildHandle->CreatePropertyValueWidget()
				]
			];
	}
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

	// Create a row generator to get live property handles
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FPropertyRowGeneratorArgs Args;
	RowGenerator = PropertyModule.CreatePropertyRowGenerator(Args);

	if (RowGenerator.IsValid())
	{
		RowGenerator->SetObjects({Coll});

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
