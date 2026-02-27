// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionCategoryGroup.h"

#include "Details/Collections/FPCGExCollectionTileDragDropOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#pragma region SPCGExCollectionCategoryGroup

void SPCGExCollectionCategoryGroup::Construct(const FArguments& InArgs)
{
	CategoryName = InArgs._CategoryName;
	OnCategoryRenamed = InArgs._OnCategoryRenamed;
	OnTileDropOnCategory = InArgs._OnTileDropOnCategory;
	OnAssetDropOnCategory = InArgs._OnAssetDropOnCategory;
	OnAddToCategory = InArgs._OnAddToCategory;

	const bool bIsUncategorized = CategoryName.IsNone();
	const FText DisplayName = bIsUncategorized ? INVTEXT("Uncategorized") : FText::FromName(CategoryName);
	const FText CountText = FText::Format(INVTEXT("({0})"), FText::AsNumber(InArgs._EntryCount));

	TSharedRef<SWidget> HeaderNameWidget = bIsUncategorized
		                                       ? StaticCastSharedRef<SWidget>(
			                                       SNew(STextBlock)
			                                       .Text(DisplayName)
			                                       .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			                                       .ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.5f))))
		                                       : StaticCastSharedRef<SWidget>(
			                                       SNew(SEditableTextBox)
			                                       .Text(DisplayName)
			                                       .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			                                       .OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType)
			                                       {
				                                       if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
				                                       {
					                                       const FName NewName = FName(*NewText.ToString());
					                                       if (NewName != CategoryName && !NewName.IsNone())
					                                       {
						                                       OnCategoryRenamed.ExecuteIfBound(CategoryName, NewName);
					                                       }
				                                       }
			                                       }));

	ChildSlot
	[
		SAssignNew(DropHighlightBorder, SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor_Lambda([this]() -> FSlateColor
		{
			return bIsDragOver
				       ? FSlateColor(FLinearColor(0.2f, 0.5f, 1.f, 0.3f))
				       : FSlateColor(FLinearColor::Transparent);
		})
		.Padding(2.f)
		[
			SAssignNew(ExpandableArea, SExpandableArea)
			.InitiallyCollapsed(InArgs._bIsCollapsed)
			.HeaderPadding(FMargin(4.f, 2.f))
			.HeaderContent()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					HeaderNameWidget
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(CountText)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.4f)))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(1, 1))
					.OnClicked_Lambda([this]() -> FReply
					{
						OnAddToCategory.ExecuteIfBound(CategoryName);
						return FReply::Handled();
					})
					.ToolTipText(INVTEXT("Add new entry to this category"))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Plus"))
						.DesiredSizeOverride(FVector2D(12, 12))
						.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.6f)))
					]
				]
			]
			.BodyContent()
			[
				SNew(SBox)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SAssignNew(TilesWrapBox, SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(4.f, 4.f))
				]
			]
		]
	];
}

void SPCGExCollectionCategoryGroup::AddTile(const TSharedRef<SWidget>& TileWidget)
{
	if (TilesWrapBox.IsValid())
	{
		TilesWrapBox->AddSlot()
			.Padding(2.f)
			[
				TileWidget
			];
	}
}

void SPCGExCollectionCategoryGroup::ClearTiles()
{
	if (TilesWrapBox.IsValid())
	{
		TilesWrapBox->ClearChildren();
	}
}

bool SPCGExCollectionCategoryGroup::IsCollapsed() const
{
	return ExpandableArea.IsValid() && !ExpandableArea->IsExpanded();
}

FReply SPCGExCollectionCategoryGroup::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	if (InDragDropEvent.GetOperationAs<FPCGExCollectionTileDragDropOp>())
	{
		bIsDragOver = true;
		return FReply::Handled();
	}
	if (const TSharedPtr<FAssetDragDropOp> AssetOp = InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		if (!AssetOp->GetAssets().IsEmpty())
		{
			bIsDragOver = true;
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SPCGExCollectionCategoryGroup::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	bIsDragOver = false;

	if (const TSharedPtr<FPCGExCollectionTileDragDropOp> TileOp = InDragDropEvent.GetOperationAs<FPCGExCollectionTileDragDropOp>())
	{
		OnTileDropOnCategory.ExecuteIfBound(CategoryName, TileOp->DraggedIndices);
		return FReply::Handled();
	}

	if (const TSharedPtr<FAssetDragDropOp> AssetOp = InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		const TArray<FAssetData>& Assets = AssetOp->GetAssets();
		if (!Assets.IsEmpty())
		{
			OnAssetDropOnCategory.ExecuteIfBound(CategoryName, Assets);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SPCGExCollectionCategoryGroup::OnDragLeave(const FDragDropEvent& InDragDropEvent)
{
	bIsDragOver = false;
	SCompoundWidget::OnDragLeave(InDragDropEvent);
}

#pragma endregion
