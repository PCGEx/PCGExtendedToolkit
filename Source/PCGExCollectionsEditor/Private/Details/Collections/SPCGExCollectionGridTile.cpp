// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionGridTile.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "PropertyHandle.h"
#include "Core/PCGExAssetCollection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void SPCGExCollectionGridTile::Construct(const FArguments& InArgs)
{
	EntryHandle = InArgs._EntryHandle;
	ThumbnailPool = InArgs._ThumbnailPool;
	TileSize = InArgs._TileSize;

	check(EntryHandle.IsValid());

	TSharedPtr<IPropertyHandle> WeightHandle = EntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Weight));
	TSharedPtr<IPropertyHandle> CategoryHandle = EntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Category));
	TSharedPtr<IPropertyHandle> IsSubCollectionHandle = EntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, bIsSubCollection));

	// Build picker widget via delegate (type-specific)
	TSharedRef<SWidget> PickerWidget = SNullWidget::NullWidget;
	if (InArgs._OnGetPickerWidget.IsBound())
	{
		PickerWidget = InArgs._OnGetPickerWidget.Execute(EntryHandle.ToSharedRef());
	}

	const float ContentWidth = TileSize + 16.f;

	ChildSlot
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
							IsSubCollectionHandle->CreatePropertyValueWidget()
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
							WeightHandle->CreatePropertyValueWidget()
						]
					]
				]

				// Thumbnail
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0, 2)
				[
					SAssignNew(ThumbnailBox, SBox)
					.WidthOverride(TileSize)
					.HeightOverride(TileSize)
					[
						BuildThumbnailWidget()
					]
				]

				// Picker (type-specific: mesh picker, actor class picker, etc.)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2)
				[
					PickerWidget
				]

				// Category
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
						CategoryHandle->CreatePropertyValueWidget()
					]
				]
			]
		]
	];
}

void SPCGExCollectionGridTile::RefreshThumbnail()
{
	if (ThumbnailBox.IsValid())
	{
		ThumbnailBox->SetContent(BuildThumbnailWidget());
	}
}

TSharedRef<SWidget> SPCGExCollectionGridTile::BuildThumbnailWidget()
{
	if (!ThumbnailPool.IsValid() || !EntryHandle.IsValid())
	{
		return SNew(SBox)
			.WidthOverride(TileSize)
			.HeightOverride(TileSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("?"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	// Check if subcollection
	bool bIsSubCollection = false;
	TSharedPtr<IPropertyHandle> IsSubCollectionHandle = EntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, bIsSubCollection));
	if (IsSubCollectionHandle.IsValid()) { IsSubCollectionHandle->GetValue(bIsSubCollection); }

	if (bIsSubCollection)
	{
		// Show a collection icon for subcollections
		return SNew(SBox)
			.WidthOverride(TileSize)
			.HeightOverride(TileSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ClassIcon.DataAsset"))
				.DesiredSizeOverride(FVector2D(48, 48))
			];
	}

	// Get Staging.Path from the entry handle
	TSharedPtr<IPropertyHandle> StagingHandle = EntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetCollectionEntry, Staging));
	if (!StagingHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	TSharedPtr<IPropertyHandle> PathHandle = StagingHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAssetStagingData, Path));
	if (!PathHandle.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	FString PathString;
	PathHandle->GetValueAsFormattedString(PathString);

	if (PathString.IsEmpty() || PathString == TEXT("None"))
	{
		return SNew(SBox)
			.WidthOverride(TileSize)
			.HeightOverride(TileSize)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(INVTEXT("No Asset"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(1, 1, 1, 0.3f)))
			];
	}

	// Resolve FAssetData from path
	const FSoftObjectPath SoftPath(PathString);
	const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(SoftPath);

	const int32 ThumbnailResolution = FMath::RoundToInt32(TileSize);
	Thumbnail = MakeShared<FAssetThumbnail>(AssetData, ThumbnailResolution, ThumbnailResolution, ThumbnailPool);

	FAssetThumbnailConfig ThumbnailConfig;
	ThumbnailConfig.bAllowFadeIn = true;

	if (Thumbnail.IsValid())
	{
		return Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}

	return SNullWidget::NullWidget;
}
