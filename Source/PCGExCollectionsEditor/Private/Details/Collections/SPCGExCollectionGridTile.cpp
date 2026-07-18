// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridTile.h"

#include "AssetThumbnail.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetData.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Collections/PCGExCollectionEditorSlateUtils.h"
#include "Details/Collections/PCGExCollectionEditorUtils.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExCollectionGrid
{
	static const FName NewCategorySentinel("__PCGEx_NewCategory__");

	/** Opacity reflects whether the effective grammar enables this axis at paint time. */
	TSharedRef<SWidget> MakeAxisHintLetter(
		TWeakObjectPtr<UPCGExAssetCollection> WeakColl,
		int32 Idx,
		EPCGExGrammarAxes Axis,
		const FText& Letter)
	{
		return SNew(STextBlock)
			.Text(Letter)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 6))
			.ColorAndOpacity_Lambda([WeakColl, Idx, Axis]() -> FSlateColor
			{
				static constexpr float DisabledAlpha = 0.15f;
				static constexpr float EnabledAlpha = 0.7f;
				const UPCGExAssetCollection* Coll = WeakColl.Get();
				if (!Coll) { return FSlateColor(FLinearColor(1, 1, 1, DisabledAlpha)); }
				const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
				if (!Result.IsValid()) { return FSlateColor(FLinearColor(1, 1, 1, DisabledAlpha)); }
				const FPCGExAssetGrammarDetails* G = Result.Entry->GetEffectiveGrammar(Coll);
				const float A = (G && G->HasAxis(Axis)) ? EnabledAlpha : DisabledAlpha;
				return FSlateColor(FLinearColor(1, 1, 1, A));
			});
	}

	TSharedRef<SWidget> MakeAxisHintRow(TWeakObjectPtr<UPCGExAssetCollection> WeakColl, int32 Idx)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 a = 0; a < 3; ++a)
		{
			Row->AddSlot()
			   .AutoWidth()
			   .Padding(a < 2 ? FMargin(0, 0, 2, 0) : FMargin(0))
			[
				MakeAxisHintLetter(
					WeakColl, Idx,
					PCGExGrammarAxes::Bits[a],
					FText::FromString(PCGExGrammarAxes::Letters[a]))
			];
		}
		return Row;
	}
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
	OnTileEntryChanged = InArgs._OnTileEntryChanged;
	ThumbnailCachePtr = InArgs._ThumbnailCachePtr;
	BatchFlagPtr = InArgs._BatchFlagPtr;

	TWeakObjectPtr<UPCGExAssetCollection> WeakColl = Collection;
	const int32 Idx = EntryIndex;

	// Shared with the variant grid tiles so badge styling stays in lockstep.
	const FSlateBrush* BadgeBrush = PCGExCollectionEditorSlateUtils::GetBadgeBrush();

	// Build picker widget via delegate (type-specific)
	TSharedRef<SWidget> PickerWidget = SNullWidget::NullWidget;
	if (InArgs._OnGetPickerWidget.IsBound())
	{
		FOnTilePropertyEdited PickerCallback = FOnTilePropertyEdited::CreateSP(this, &SPCGExCollectionGridTile::HandlePickerPropertyEdited);
		PickerWidget = InArgs._OnGetPickerWidget.Execute(Collection, EntryIndex, PickerCallback);
	}

	// Build category widget -- combobox with "New..." option
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
					if (!Selected.IsValid() || SelectType == ESelectInfo::Direct)
					{
						return;
					}

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
					if (!Coll)
					{
						return;
					}

					FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
					if (!Entry)
					{
						return;
					}

					if (BatchFlagPtr)
					{
						*BatchFlagPtr = true;
					}
					{
						FScopedTransaction Transaction(INVTEXT("Change Category"));
						Coll->Modify();
						Entry->Category = *Selected;
						Coll->PostEditChange();
					}
					OnTileEntryChanged.ExecuteIfBound(Idx, GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category));
					if (BatchFlagPtr)
					{
						*BatchFlagPtr = false;
					}
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
					// Content (header button) -- shows current category
					SNew(STextBlock)
					.Text_Lambda([WeakColl, Idx]() -> FText
					{
						const UPCGExAssetCollection* Coll = WeakColl.Get();
						if (!Coll)
						{
							return INVTEXT("?");
						}
						const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
						if (!Result.IsValid())
						{
							return INVTEXT("?");
						}
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
								if (BatchFlagPtr)
								{
									*BatchFlagPtr = true;
								}
								{
									FScopedTransaction Transaction(INVTEXT("New Category"));
									Coll->Modify();
									Entry->Category = NewCat;
									Coll->PostEditChange();
								}
								OnTileEntryChanged.ExecuteIfBound(Idx, GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category));
								if (BatchFlagPtr)
								{
									*BatchFlagPtr = false;
								}
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
				if (Result.IsValid())
				{
					CurrentCategory = Result.Entry->Category;
				}
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
	const FText PrimaryIndexText = FText::AsNumber(EntryIndex);
	const bool bHasCategoryIndex = (CategoryIndex != INDEX_NONE);

	ChildSlot
	[
		// Selection highlight border (outermost)
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.White"))
		.BorderBackgroundColor_Lambda([this]() -> FSlateColor
		{
			return bIsSelected
				? FSlateColor(FLinearColor(0.1f, 0.4f, 0.9f, 1.0f))
				: FSlateColor(FLinearColor(0, 0, 0, 0));
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
									if (!Coll)
									{
										return ECheckBoxState::Unchecked;
									}
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid())
									{
										return ECheckBoxState::Unchecked;
									}
									return Result.Entry->bIsSubCollection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
								.OnCheckStateChanged_Lambda([this, WeakColl, Idx](ECheckBoxState NewState)
								{
									UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return;
									}
									FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
									if (!Entry)
									{
										return;
									}
									if (BatchFlagPtr)
									{
										*BatchFlagPtr = true;
									}
									{
										FScopedTransaction Transaction(INVTEXT("Toggle SubCollection"));
										Coll->Modify();
										Entry->bIsSubCollection = (NewState == ECheckBoxState::Checked);
										Coll->PostEditChange();
									}
									OnTileEntryChanged.ExecuteIfBound(Idx, GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, bIsSubCollection));
									if (BatchFlagPtr)
									{
										*BatchFlagPtr = false;
									}
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
								.Value_Lambda([WeakColl, Idx]() -> int32
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return 0;
									}
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									return Result.IsValid() ? Result.Entry->Weight : 0;
								})
								.OnBeginSliderMovement_Lambda([this, WeakColl]()
								{
									if (BatchFlagPtr)
									{
										*BatchFlagPtr = true;
									}
									if (GEditor)
									{
										GEditor->BeginTransaction(INVTEXT("Adjust Weight"));
									}
									if (UPCGExAssetCollection* Coll = WeakColl.Get())
									{
										Coll->Modify();
									}
								})
								.OnValueChanged_Lambda([WeakColl, Idx](int32 NewVal)
								{
									UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return;
									}
									FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
									if (Entry)
									{
										Entry->Weight = NewVal;
									}
								})
								.OnEndSliderMovement_Lambda([this, WeakColl, Idx](int32)
								{
									if (UPCGExAssetCollection* Coll = WeakColl.Get())
									{
										Coll->PostEditChange();
									}
									if (GEditor)
									{
										GEditor->EndTransaction();
									}
									OnTileEntryChanged.ExecuteIfBound(Idx, GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Weight));
									if (BatchFlagPtr)
									{
										*BatchFlagPtr = false;
									}
								})
								.OnValueCommitted_Lambda([this, WeakColl, Idx](int32 NewVal, ETextCommit::Type CommitType)
								{
									UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return;
									}
									FPCGExAssetCollectionEntry* Entry = Coll->EDITOR_GetMutableEntry(Idx);
									if (!Entry)
									{
										return;
									}
									if (BatchFlagPtr)
									{
										*BatchFlagPtr = true;
									}
									{
										FScopedTransaction Transaction(INVTEXT("Set Weight"));
										Coll->Modify();
										Entry->Weight = NewVal;
										Coll->PostEditChange();
									}
									OnTileEntryChanged.ExecuteIfBound(Idx, GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Weight));
									if (BatchFlagPtr)
									{
										*BatchFlagPtr = false;
									}
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

						// Index tag (top-left)
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						.Padding(2.f)
						[
							SNew(SBorder)
							.BorderImage(BadgeBrush)
							.BorderBackgroundColor(FLinearColor(0, 0, 0, 0.7f))
							.Padding(FMargin(5, 2))
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(STextBlock)
									.Text(PrimaryIndexText)
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]

								+ SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(STextBlock)
									.Text(FText::Format(INVTEXT(" | {0}"), FText::AsNumber(CategoryIndex)))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
									.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.45f)))
									.Visibility(bHasCategoryIndex ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
								]
							]
						]

						// Meta badges (top-right)
						+ SOverlay::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Top)
						.Padding(2.f)
						[
							SNew(SHorizontalBox)

							// Variations badge
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(1, 0)
							[
								SNew(SBorder)
								.Visibility_Lambda([WeakColl, Idx]() -> EVisibility
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return EVisibility::Collapsed;
									}
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid() || Result.Entry->bIsSubCollection)
									{
										return EVisibility::Collapsed;
									}
									const FPCGExFittingVariations& V = Result.Entry->Variations;
									const bool bHasVariations =
										V.OffsetMin != FVector::ZeroVector || V.OffsetMax != FVector::ZeroVector ||
										V.RotationMin != FRotator::ZeroRotator || V.RotationMax != FRotator::ZeroRotator ||
										V.ScaleMin != FVector::OneVector || V.ScaleMax != FVector::OneVector;
									return bHasVariations ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
								})
								.BorderImage(BadgeBrush)
								.BorderBackgroundColor(FLinearColor(0.6f, 0.4f, 0.1f, 0.85f))
								.Padding(FMargin(3, 1))
								[
									SNew(STextBlock)
									.Text(INVTEXT("V"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 6))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]
							]

							// Sockets badge
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(1, 0)
							[
								SNew(SBorder)
								.Visibility_Lambda([WeakColl, Idx]() -> EVisibility
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return EVisibility::Collapsed;
									}
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid() || Result.Entry->bIsSubCollection)
									{
										return EVisibility::Collapsed;
									}
									return !Result.Entry->Staging.Sockets.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
								})
								.BorderImage(BadgeBrush)
								.BorderBackgroundColor(FLinearColor(0.1f, 0.5f, 0.6f, 0.85f))
								.Padding(FMargin(3, 1))
								[
									SNew(STextBlock)
									.Text_Lambda([WeakColl, Idx]() -> FText
									{
										const UPCGExAssetCollection* Coll = WeakColl.Get();
										if (!Coll)
										{
											return FText::GetEmpty();
										}
										const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
										if (!Result.IsValid())
										{
											return FText::GetEmpty();
										}
										return FText::Format(INVTEXT("S:{0}"), FText::AsNumber(Result.Entry->Staging.Sockets.Num()));
									})
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 6))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]
							]

							// Tags badge
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(1, 0)
							[
								SNew(SBorder)
								.Visibility_Lambda([WeakColl, Idx]() -> EVisibility
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return EVisibility::Collapsed;
									}
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid())
									{
										return EVisibility::Collapsed;
									}
									return !Result.Entry->Tags.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
								})
								.BorderImage(BadgeBrush)
								.BorderBackgroundColor(FLinearColor(0.4f, 0.2f, 0.6f, 0.85f))
								.Padding(FMargin(3, 1))
								[
									SNew(STextBlock)
									.Text(INVTEXT("T"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 6))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]
							]

							// PropertyOverrides badge
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(1, 0)
							[
								SNew(SBorder)
								.Visibility_Lambda([WeakColl, Idx]() -> EVisibility
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll)
									{
										return EVisibility::Collapsed;
									}
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid() || Result.Entry->bIsSubCollection)
									{
										return EVisibility::Collapsed;
									}
									return Result.Entry->PropertyOverrides.GetEnabledCount() > 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
								})
								.BorderImage(BadgeBrush)
								.BorderBackgroundColor(FLinearColor(0.2f, 0.6f, 0.3f, 0.85f))
								.Padding(FMargin(3, 1))
								[
									SNew(STextBlock)
									.Text(INVTEXT("P"))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 6))
									.ColorAndOpacity(FSlateColor(FLinearColor::White))
								]
							]
						]

						// Grammar column: resolves via GetEffectiveGrammar so SubCollection Inherit/Flatten
						// and leaf Global/Overrule track what the export pipeline actually emits.
						// Extra bottom padding clears UE's native asset-type color strip along the
						// thumbnail edge.
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Bottom)
						.Padding(FMargin(2.f, 2.f, 2.f, 6.f))
						[
							SNew(SVerticalBox)
							.Visibility_Lambda([WeakColl, Idx]() -> EVisibility
							{
								const UPCGExAssetCollection* Coll = WeakColl.Get();
								if (!Coll) { return EVisibility::Collapsed; }
								const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
								if (!Result.IsValid()) { return EVisibility::Collapsed; }
								const FPCGExAssetGrammarDetails* G = Result.Entry->GetEffectiveGrammar(Coll);
								// nullptr = Flatten or unbound sub-collection (no module emitted); empty Symbol = nothing to label.
								// Axis state is deliberately not gated -- the symbol shows even with no axis enabled.
								if (!G || G->Symbol.IsNone()) { return EVisibility::Collapsed; }
								return EVisibility::HitTestInvisible;
							})

							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Left)
							.Padding(FMargin(1, 0, 0, 1))
							[
								PCGExCollectionGrid::MakeAxisHintRow(WeakColl, Idx)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SBorder)
								.BorderImage(BadgeBrush)
								.BorderBackgroundColor_Lambda([WeakColl, Idx]() -> FSlateColor
								{
									const UPCGExAssetCollection* Coll = WeakColl.Get();
									if (!Coll) { return FSlateColor(FLinearColor(0, 0, 0, 0.85f)); }
									const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
									if (!Result.IsValid()) { return FSlateColor(FLinearColor(0, 0, 0, 0.85f)); }
									const FPCGExAssetGrammarDetails* G = Result.Entry->GetEffectiveGrammar(Coll);
									if (!G) { return FSlateColor(FLinearColor(0, 0, 0, 0.85f)); }
									FLinearColor C = G->DebugColor;
									C.A = 0.85f;
									return FSlateColor(C);
								})
								.Padding(FMargin(3, 1))
								[
									SNew(STextBlock)
									.Text_Lambda([WeakColl, Idx]() -> FText
									{
										const UPCGExAssetCollection* Coll = WeakColl.Get();
										if (!Coll) { return FText::GetEmpty(); }
										const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
										if (!Result.IsValid()) { return FText::GetEmpty(); }
										const FPCGExAssetGrammarDetails* G = Result.Entry->GetEffectiveGrammar(Coll);
										return G ? FText::FromName(G->Symbol) : FText::GetEmpty();
									})
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
									.ColorAndOpacity_Lambda([WeakColl, Idx]() -> FSlateColor
									{
										const UPCGExAssetCollection* Coll = WeakColl.Get();
										if (!Coll) { return FSlateColor(FLinearColor::White); }
										const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(Idx);
										if (!Result.IsValid()) { return FSlateColor(FLinearColor::White); }
										const FPCGExAssetGrammarDetails* G = Result.Entry->GetEffectiveGrammar(Coll);
										if (!G) { return FSlateColor(FLinearColor::White); }
										return PCGExCollectionEditorSlateUtils::PickReadableTextColor(G->DebugColor);
									})
								]
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
						SNew(SBorder)
						.BorderImage(FAppStyle::GetNoBrush())
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f))
						.Padding(0)
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
		// Deferred exclusive select -- was already selected, user didn't drag
		OnTileClicked.ExecuteIfBound(EntryIndex, MouseEvent);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPCGExCollectionGridTile::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// Suppress the deferred selection toggle that OnMouseButtonDown armed -- otherwise
	// the second click's mouse-up would clear the selection we want to keep.
	bPendingClick = false;

	const UPCGExAssetCollection* Coll = Collection.Get();
	if (!Coll || EntryIndex == INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(EntryIndex);
	if (!Result.IsValid())
	{
		return FReply::Unhandled();
	}

	const FSoftObjectPath AssetPath = Result.Entry->EDITOR_GetThumbnailAssetPath();
	if (AssetPath.IsNull())
	{
		return FReply::Unhandled();
	}

	if (UObject* Asset = AssetPath.TryLoad())
	{
		if (GEditor)
		{
			if (UAssetEditorSubsystem* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditor->OpenEditorForAsset(Asset);
			}
		}
	}

	return FReply::Handled();
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

void SPCGExCollectionGridTile::HandlePickerPropertyEdited(FName PropertyName)
{
	RefreshThumbnail();
	OnTileEntryChanged.ExecuteIfBound(EntryIndex, PropertyName);
}

void SPCGExCollectionGridTile::RefreshThumbnail()
{
	if (!ThumbnailBox.IsValid())
	{
		return;
	}

	// Check if the visual state has actually changed
	const UPCGExAssetCollection* Coll = Collection.Get();
	if (Coll && EntryIndex != INDEX_NONE)
	{
		const FPCGExEntryAccessResult Result = Coll->GetEntryRaw(EntryIndex);
		if (Result.IsValid())
		{
			const bool bIsSub = Result.Entry->bIsSubCollection;
			const FSoftObjectPath CurrentPath = Result.Entry->EDITOR_GetThumbnailAssetPath();

			if (CurrentPath == CachedThumbnailPath && bIsSub == bCachedIsSubCollection)
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
		CachedThumbnailPath.Reset();
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
	CachedThumbnailPath = Result.Entry->EDITOR_GetThumbnailAssetPath();

	// Subcollection entries fall through to the FAssetThumbnail path; the renderer draws their mosaic.

	// Get asset path -- entry-specific source (may differ from Staging.Path for
	// entry types that bake into an embedded asset, e.g. level-sourced PCGDataAsset).
	const FSoftObjectPath& AssetPath = CachedThumbnailPath;
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

	const FAssetData AssetData = PCGExCollectionEditorUtils::ResolveEntryAssetData(AssetPath);

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
