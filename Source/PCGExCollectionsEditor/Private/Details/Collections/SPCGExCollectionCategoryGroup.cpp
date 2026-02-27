// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionCategoryGroup.h"

#include "Details/Collections/FPCGExCollectionTileDragDropOp.h"
#include "Widgets/SOverlay.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
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
	OnExpansionChanged = InArgs._OnExpansionChanged;
	OnTileReorderInCategory = InArgs._OnTileReorderInCategory;
	bIsCollapsed = InArgs._bIsCollapsed;

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
		.BorderImage(FAppStyle::GetBrush("Brushes.White"))
		.BorderBackgroundColor_Lambda([this]() -> FSlateColor
		{
			return bIsDragOver
				       ? FSlateColor(FLinearColor(0.2f, 0.5f, 1.f, 0.3f))
				       : FSlateColor(FLinearColor::Transparent);
		})
		.Padding(0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.f))
			.Padding(FMargin(6.f, 4.f))
			[
				SNew(SVerticalBox)

				// Header row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 2)
				[
					SNew(SHorizontalBox)

					// Collapse arrow
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(0))
						.OnClicked_Lambda([this]() -> FReply
						{
							bIsCollapsed = !bIsCollapsed;
							if (BodyContainer.IsValid())
							{
								BodyContainer->SetVisibility(bIsCollapsed ? EVisibility::Collapsed : EVisibility::Visible);
							}
							if (CollapseArrow.IsValid())
							{
								CollapseArrow->SetImage(FAppStyle::GetBrush(
									bIsCollapsed ? "TreeArrow_Collapsed" : "TreeArrow_Expanded"));
							}
							OnExpansionChanged.ExecuteIfBound(CategoryName, !bIsCollapsed);
							return FReply::Handled();
						})
						[
							SAssignNew(CollapseArrow, SImage)
							.Image(FAppStyle::GetBrush(bIsCollapsed ? "TreeArrow_Collapsed" : "TreeArrow_Expanded"))
							.DesiredSizeOverride(FVector2D(10, 10))
							.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.7f)))
						]
					]

					// Category name
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						HeaderNameWidget
					]

					// Count
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
					.FillWidth(1.f)
					[
						SNullWidget::NullWidget
					]

					// Add button
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

				// Body (tiles wrap box)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(BodyContainer, SBox)
					.Visibility(bIsCollapsed ? EVisibility::Collapsed : EVisibility::Visible)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						[
							SNew(SBox)
							.Clipping(EWidgetClipping::ClipToBounds)
							[
								SAssignNew(TilesWrapBox, SWrapBox)
								.UseAllottedSize(true)
								.InnerSlotPadding(FVector2D(4.f, 4.f))
							]
						]

						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Top)
						[
							SAssignNew(InsertIndicator, SBox)
							.Visibility(EVisibility::Collapsed)
							.WidthOverride(3.f)
							.HeightOverride(1.f)
							[
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(FLinearColor(0.3f, 0.6f, 1.f, 0.8f))
								.Padding(0)
							]
						]
					]
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

FReply SPCGExCollectionCategoryGroup::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	if (const TSharedPtr<FPCGExCollectionTileDragDropOp> TileOp = InDragDropEvent.GetOperationAs<FPCGExCollectionTileDragDropOp>())
	{
		bIsDragOver = true;

		const int32 OldDropInsertIndex = DropInsertIndex;

		// Compute insertion position for reorder (same-category or cross-category)
		if (TilesWrapBox.IsValid())
		{
			const FVector2D MouseAbsPos = InDragDropEvent.GetScreenSpacePosition();
			FChildren* Children = TilesWrapBox->GetChildren();
			const int32 NumChildren = Children->Num();

			if (NumChildren > 0)
			{
				int32 ClosestTileIndex = 0;
				float ClosestDist = FLT_MAX;

				for (int32 i = 0; i < NumChildren; i++)
				{
					const TSharedRef<SWidget>& Child = Children->GetChildAt(i);
					const FGeometry& ChildGeo = Child->GetCachedGeometry();
					const FVector2D Center = ChildGeo.GetAbsolutePosition() + ChildGeo.GetAbsoluteSize() * 0.5f;

					const float Dist = FVector2D::Distance(MouseAbsPos, Center);
					if (Dist < ClosestDist)
					{
						ClosestDist = Dist;
						ClosestTileIndex = i;
					}
				}

				// Determine if mouse is on left or right half of closest tile
				const TSharedRef<SWidget>& ClosestChild = Children->GetChildAt(ClosestTileIndex);
				const FGeometry& ClosestGeo = ClosestChild->GetCachedGeometry();
				const float CenterX = ClosestGeo.GetAbsolutePosition().X + ClosestGeo.GetAbsoluteSize().X * 0.5f;

				DropInsertIndex = (MouseAbsPos.X > CenterX) ? ClosestTileIndex + 1 : ClosestTileIndex;
			}
			else
			{
				DropInsertIndex = 0;
			}
		}
		else
		{
			DropInsertIndex = INDEX_NONE;
		}

		// Update insertion indicator widget when position changes
		if (DropInsertIndex != OldDropInsertIndex && InsertIndicator.IsValid())
		{
			if (DropInsertIndex != INDEX_NONE)
			{
				FChildren* IndicChildren = TilesWrapBox->GetChildren();
				const int32 IndicNumChildren = IndicChildren->Num();

				FVector2D LineAbsPos;
				float AbsHeight = 0;

				if (DropInsertIndex >= IndicNumChildren)
				{
					// After last tile: right edge
					const TSharedRef<SWidget>& LastChild = IndicChildren->GetChildAt(IndicNumChildren - 1);
					const FGeometry& ChildGeo = LastChild->GetCachedGeometry();
					LineAbsPos = ChildGeo.GetAbsolutePosition() + FVector2D(ChildGeo.GetAbsoluteSize().X + 2.f, 0);
					AbsHeight = ChildGeo.GetAbsoluteSize().Y;
				}
				else
				{
					// Before tile at DropInsertIndex: left edge
					const TSharedRef<SWidget>& Child = IndicChildren->GetChildAt(DropInsertIndex);
					const FGeometry& ChildGeo = Child->GetCachedGeometry();
					LineAbsPos = ChildGeo.GetAbsolutePosition() - FVector2D(2.f, 0);
					AbsHeight = ChildGeo.GetAbsoluteSize().Y;
				}

				if (AbsHeight > 0)
				{
					const FGeometry& WrapGeo = TilesWrapBox->GetCachedGeometry();
					const FVector2D LocalPos = WrapGeo.AbsoluteToLocal(LineAbsPos);
					const FVector2D LocalPosEnd = WrapGeo.AbsoluteToLocal(LineAbsPos + FVector2D(0, AbsHeight));
					const float LocalHeight = LocalPosEnd.Y - LocalPos.Y;

					InsertIndicator->SetHeightOverride(LocalHeight);
					InsertIndicator->SetRenderTransform(FSlateRenderTransform(FVector2D(LocalPos.X - 1.5f, LocalPos.Y)));
					InsertIndicator->SetVisibility(EVisibility::HitTestInvisible);
				}
			}
			else
			{
				InsertIndicator->SetVisibility(EVisibility::Collapsed);
			}
		}

		return FReply::Handled();
	}
	if (const TSharedPtr<FAssetDragDropOp> AssetOp = InDragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		if (!AssetOp->GetAssets().IsEmpty())
		{
			bIsDragOver = true;
			DropInsertIndex = INDEX_NONE;
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SPCGExCollectionCategoryGroup::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent)
{
	bIsDragOver = false;
	const int32 CapturedInsertIndex = DropInsertIndex;
	DropInsertIndex = INDEX_NONE;
	if (InsertIndicator.IsValid()) { InsertIndicator->SetVisibility(EVisibility::Collapsed); }

	if (const TSharedPtr<FPCGExCollectionTileDragDropOp> TileOp = InDragDropEvent.GetOperationAs<FPCGExCollectionTileDragDropOp>())
	{
		if (TileOp->SourceCategory == CategoryName && CapturedInsertIndex != INDEX_NONE)
		{
			// Same-category drop with valid insertion index → reorder
			OnTileReorderInCategory.ExecuteIfBound(CategoryName, TileOp->DraggedIndices, CapturedInsertIndex);
		}
		else
		{
			// Cross-category drop → change category + position
			OnTileDropOnCategory.ExecuteIfBound(CategoryName, TileOp->DraggedIndices, CapturedInsertIndex);
		}
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
	DropInsertIndex = INDEX_NONE;
	if (InsertIndicator.IsValid()) { InsertIndicator->SetVisibility(EVisibility::Collapsed); }
	SCompoundWidget::OnDragLeave(InDragDropEvent);
}

#pragma endregion
