// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridTile.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Core/PCGExAssetCollection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExCollectionGrid
{
	static const FName NewCategorySentinel("__PCGEx_NewCategory__");
}

#pragma region SPCGExCollectionGridTile

void SPCGExCollectionGridTile::Construct(const FArguments& InArgs)
{
	ThumbnailPool = InArgs._ThumbnailPool;
	TileSize = InArgs._TileSize;
	Collection = InArgs._Collection;
	EntryIndex = InArgs._EntryIndex;
	CategoryIndex = InArgs._CategoryIndex;
	CategoryOptions = InArgs._CategoryOptions;
	OnTileClicked = InArgs._OnTileClicked;
	OnTileDragDetected = InArgs._OnTileDragDetected;
	OnTileCategoryChanged = InArgs._OnTileCategoryChanged;
	ThumbnailCachePtr = InArgs._ThumbnailCachePtr;
	BatchFlagPtr = InArgs._BatchFlagPtr;

	TWeakObjectPtr<UPCGExAssetCollection> WeakColl = Collection;
	const int32 Idx = EntryIndex;

	// Build picker widget via delegate (type-specific)
	TSharedRef<SWidget> PickerWidget = SNullWidget::NullWidget;
	if (InArgs._OnGetPickerWidget.IsBound())
	{
		FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateSP(this, &SPCGExCollectionGridTile::RefreshThumbnail);
		PickerWidget = InArgs._OnGetPickerWidget.Execute(Collection, EntryIndex, RefreshDelegate);
	}

	// Build category widget — combobox with "New..." option
	TSharedRef<SWidget> CategoryWidget = SNullWidget::NullWidget;
	if (CategoryOptions.IsValid())
	{
		CategoryWidget =
			SAssignNew(CategoryWidgetSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)

			// Index 0: Combobox
			+ SWidgetSwitcher::Slot()
			[
				SAssignNew(CategoryCombo, SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&(*CategoryOptions))
				.OnSelectionChanged_Lambda([this, WeakColl, Idx](TSharedPtr<FName> Selected, ESelectInfo::Type SelectType)
				{
					if (!Selected.IsValid() || SelectType == ESelectInfo::Direct) { return; }

					if (*Selected == PCGExCollectionGrid::NewCategorySentinel)
					{
						// Switch to text entry mode
						if (CategoryWidgetSwitcher.IsValid())
						{
							CategoryWidgetSwitcher->SetActiveWidgetIndex(1);
						}
						return;
					}

					// Set the category value
					UPCGExAssetCollection* Coll = WeakColl.Get();
					if (!Coll) { return; }

					FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
					if (!Entry) { return; }

					if (BatchFlagPtr) { *BatchFlagPtr = true; }
					FScopedTransaction Transaction(INVTEXT("Change Category"));
					Coll->Modify();
					Entry->Category = *Selected;
					Coll->PostEditChange();
					if (BatchFlagPtr) { *BatchFlagPtr = false; }
					OnTileCategoryChanged.ExecuteIfBound();
				})
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> Item) -> TSharedRef<SWidget>
				{
					FText DisplayText;
					if (!Item.IsValid() || Item->IsNone())
					{
						DisplayText = INVTEXT("Uncategorized");
					}
					else if (*Item == PCGExCollectionGrid::NewCategorySentinel)
					{
						DisplayText = INVTEXT("+ New...");
					}
					else
					{
						DisplayText = FText::FromName(*Item);
					}
					return SNew(STextBlock)
						.Text(DisplayText)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
				})
				[
					// Content (header button) — shows current category
					SNew(STextBlock)
					.Text_Lambda([WeakColl, Idx]() -> FText
					{
						const UPCGExAssetCollection* Coll = WeakColl.Get();
						if (!Coll) { return INVTEXT("?"); }
						const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
						if (!Result.IsValid()) { return INVTEXT("?"); }
						return Result.Entry->Category.IsNone() ? INVTEXT("Uncategorized") : FText::FromName(Result.Entry->Category);
					})
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				]
			]

			// Index 1: Editable text box for new category
			+ SWidgetSwitcher::Slot()
			[
				SNew(SEditableTextBox)
				.HintText(INVTEXT("New category..."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
				.OnTextCommitted_Lambda([this, WeakColl, Idx](const FText& Text, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter && !Text.IsEmpty())
					{
						UPCGExAssetCollection* Coll = WeakColl.Get();
						if (Coll)
						{
							FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
							if (Entry)
							{
								const FName NewCat = FName(*Text.ToString());
								if (BatchFlagPtr) { *BatchFlagPtr = true; }
								FScopedTransaction Transaction(INVTEXT("New Category"));
								Coll->Modify();
								Entry->Category = NewCat;
								Coll->PostEditChange();
								if (BatchFlagPtr) { *BatchFlagPtr = false; }
								OnTileCategoryChanged.ExecuteIfBound();
							}
						}
					}
					// Switch back to combobox mode
					if (CategoryWidgetSwitcher.IsValid())
					{
						CategoryWidgetSwitcher->SetActiveWidgetIndex(0);
					}
				})
			];

		// Set initial combobox selection to match current category
		if (CategoryCombo.IsValid())
		{
			FName CurrentCategory;
			if (const UPCGExAssetCollection* Coll = WeakColl.Get())
			{
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				if (Result.IsValid()) { CurrentCategory = Result.Entry->Category; }
			}
			for (const TSharedPtr<FName>& Option : *CategoryOptions)
			{
				if (Option.IsValid() && *Option == CurrentCategory)
				{
					CategoryCombo->SetSelectedItem(Option);
					break;
				}
			}
		}
	}

	const float ContentWidth = TileSize + 16.f;

	// Index overlay text
	const FText IndexOverlayText = (CategoryIndex != INDEX_NONE)
		                               ? FText::Format(INVTEXT("[{0}|{1}]"), FText::AsNumber(EntryIndex), FText::AsNumber(CategoryIndex))
		                               : FText::Format(INVTEXT("[{0}]"), FText::AsNumber(EntryIndex));

	ChildSlot
	[
		// Selection highlight border (outermost)
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.White"))
		.BorderBackgroundColor_Lambda([this]() -> FSlateColor
		{
			return bIsSelected
				       ? FSlateColor(FLinearColor(0.15f, 0.4f, 0.8f, 0.4f))
				       : FSlateColor(FLinearColor::Transparent);
		})
		.Padding(2.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(4.f)
			[
				SNew(SBox)
				.WidthOverride(ContentWidth)
				[
					SNew(SVerticalBox)

					// Top bar: SubCollection checkbox + Weight spinner
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 2)
					[
						SNew(SHorizontalBox)

						// SubCollection checkbox
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 4, 0)
						[
							SNew(SBox)
							.ToolTipText(INVTEXT("Sub-collection"))
							[
								SNew(SCheckBox)
								.IsChecked_Lambda([WeakColl, Idx]() -> ECheckBoxState
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll) { return ECheckBoxState::Unchecked; }
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid()) { return ECheckBoxState::Unchecked; }
									return Result.Entry->bIsSubCollection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged_Lambda([this, WeakColl, Idx](ECheckBoxState NewState)
								{
									UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll) { return; }
									FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
									if (!Entry) { return; }
									if (BatchFlagPtr) { *BatchFlagPtr = true; }
									FScopedTransaction Transaction(INVTEXT("Toggle SubCollection"));
									Coll->Modify();
									Entry->bIsSubCollection = (NewState == ECheckBoxState::Checked);
									Coll->PostEditChange();
									if (BatchFlagPtr) { *BatchFlagPtr = false; }
									RefreshThumbnail();
								})
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 4, 0)
						[
							SNew(STextBlock)
							.Text(INVTEXT("Sub"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.5f)))
						]

						// Weight
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.ToolTipText(INVTEXT("Weight"))
							[
								SNew(SSpinBox<int32>)
								.MinValue(0)
								.Delta(1)
								.SliderExponent(2)
								.Value_Lambda([WeakColl, Idx]() -> int32
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll) { return 0; }
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									return Result.IsValid() ? Result.Entry->Weight : 0;
								})
								.OnBeginSliderMovement_Lambda([this, WeakColl]()
								{
									if (BatchFlagPtr) { *BatchFlagPtr = true; }
									if (GEditor) { GEditor->BeginTransaction(INVTEXT("Adjust Weight")); }
									if (UPCGExAssetCollection* Coll = WeakColl.Get()) { Coll->Modify(); }
								})
								.OnValueChanged_Lambda([WeakColl, Idx](int32 NewVal)
								{
									UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll) { return; }
									FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
									if (Entry) { Entry->Weight = NewVal; }
								})
								.OnEndSliderMovement_Lambda([this, WeakColl](int32)
								{
									if (UPCGExAssetCollection* Coll = WeakColl.Get()) { Coll->PostEditChange(); }
									if (GEditor) { GEditor->EndTransaction(); }
									if (BatchFlagPtr) { *BatchFlagPtr = false; }
								})
								.OnValueCommitted_Lambda([this, WeakColl, Idx](int32 NewVal, ETextCommit::Type CommitType)
								{
									UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll) { return; }
									FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
									if (!Entry) { return; }
									if (BatchFlagPtr) { *BatchFlagPtr = true; }
									FScopedTransaction Transaction(INVTEXT("Set Weight"));
									Coll->Modify();
									Entry->Weight = NewVal;
									Coll->PostEditChange();
									if (BatchFlagPtr) { *BatchFlagPtr = false; }
								})
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
					]

					// Thumbnail with [i|j] overlay
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.HAlign(HAlign_Center)
					.Padding(0, 2)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						[
							SAssignNew(ThumbnailBox, SBox)
							.WidthOverride(TileSize)
							.HeightOverride(TileSize)
							.Clipping(EWidgetClipping::ClipToBounds)
							[
								BuildThumbnailWidget()
							]
						]

						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						.Padding(2.f)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("Brushes.White"))
							.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.7f))
							.Padding(FMargin(3, 1))
							[
								SNew(STextBlock)
								.Text(IndexOverlayText)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
							]
						]
					]

					// Picker (type-specific: mesh picker, actor class picker, etc.)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 2)
					[
						PickerWidget
					]

					// Category combobox
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 2, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 4, 0)
						[
							SNew(STextBlock)
							.Text(INVTEXT("Cat"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.5f)))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						[
							CategoryWidget
						]
					]
				]
			]
		]
	];
}

FReply SPCGExCollectionGridTile::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// If modifier keys or unselected: apply selection immediately
		// If already selected without modifiers: defer to mouse-up (preserves multi-select for drag)
		if (MouseEvent.IsControlDown() || MouseEvent.IsShiftDown() || !bIsSelected)
		{
			OnTileClicked.ExecuteIfBound(EntryIndex, MouseEvent);
			bPendingClick = false;
		}
		else
		{
			bPendingClick = true;
		}

		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

FReply SPCGExCollectionGridTile::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bPendingClick)
	{
		bPendingClick = false;
		// Deferred exclusive select — was already selected, user didn't drag
		OnTileClicked.ExecuteIfBound(EntryIndex, MouseEvent);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPCGExCollectionGridTile::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bPendingClick = false;
	if (OnTileDragDetected.IsBound())
	{
		return OnTileDragDetected.Execute(EntryIndex, MouseEvent);
	}
	return FReply::Unhandled();
}

void SPCGExCollectionGridTile::RefreshThumbnail()
{
	if (!ThumbnailBox.IsValid()) { return; }

	// Check if the visual state has actually changed
	const UPCGExAssetCollection* Coll = Collection.Get();
	if (Coll && EntryIndex != INDEX_NONE)
	{
		const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(EntryIndex);
		if (Result.IsValid())
		{
			const bool bIsSub = Result.Entry->bIsSubCollection;
			const FSoftObjectPath& CurrentPath = Result.Entry->Staging.Path;

			if (CurrentPath == CachedStagingPath && bIsSub == bCachedIsSubCollection)
			{
				return; // Nothing visual changed, skip rebuild
			}
		}
	}

	ThumbnailBox->SetContent(BuildThumbnailWidget());
}

TSharedRef<SWidget> SPCGExCollectionGridTile::BuildThumbnailWidget()
{
	if (!ThumbnailPool.IsValid())
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("?"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	// Read staging data directly from the collection UObject
	const UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || EntryIndex == INDEX_NONE)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("?"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(EntryIndex);
	if (!Result.IsValid())
	{
		CachedStagingPath.Reset();
		bCachedIsSubCollection = false;
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("Invalid"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	// Update cache
	bCachedIsSubCollection = Result.Entry->bIsSubCollection;
	CachedStagingPath = Result.Entry->Staging.Path;

	// Subcollection — show collection icon
	if (Result.Entry->bIsSubCollection)
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ClassIcon.DataAsset"))
				.DesiredSizeOverride(FVector2D(48, 48))
			];
	}

	// Get asset path from staging data
	const FSoftObjectPath& AssetPath = CachedStagingPath;
	if (AssetPath.IsNull())
	{
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("No Asset"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	// Check cache first
	if (ThumbnailCachePtr)
	{
		if (TSharedPtr<FAssetThumbnail>* Cached = ThumbnailCachePtr->Find(AssetPath))
		{
			Thumbnail = *Cached;
			FAssetThumbnailConfig ThumbnailConfig;
			ThumbnailConfig.bAllowFadeIn = false;
			return Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
		}
	}

	// Resolve FAssetData from path and create thumbnail
	const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPath);

	const int32 ThumbnailResolution = FMath::RoundToInt32(TileSize);
	Thumbnail = MakeShared<FAssetThumbnail>(AssetData, ThumbnailResolution, ThumbnailResolution, ThumbnailPool);

	// Store in cache
	if (ThumbnailCachePtr)
	{
		ThumbnailCachePtr->Add(AssetPath, Thumbnail);
	}

	if (Thumbnail.IsValid())
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		return Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}

	return SNullWidget::NullWidget;
}

#pragma endregion
