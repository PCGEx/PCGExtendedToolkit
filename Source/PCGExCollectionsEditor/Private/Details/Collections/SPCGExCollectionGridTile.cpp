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
	Collection = InArgs._Collection;
	EntryIndex = InArgs._EntryIndex;

	check(EntryHandle.IsValid());

	// Listen for property changes to refresh the thumbnail when the asset changes
	EntryHandle->SetOnChildPropertyValueChanged(
		FSimpleDelegate::CreateSP(this, &SPCGExCollectionGridTile::RefreshThumbnail));

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

				// Thumbnail — fills remaining vertical space, clips to square
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				.HAlign(HAlign_Center)
				.Padding(0, 2)
				[
					SAssignNew(ThumbnailBox, SBox)
					.WidthOverride(TileSize)
					.Clipping(EWidgetClipping::ClipToBounds)
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
	const FSoftObjectPath& AssetPath = Result.Entry->Staging.Path;
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

	// Resolve FAssetData from path and create thumbnail
	const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(AssetPath);

	const int32 ThumbnailResolution = FMath::RoundToInt32(TileSize);
	Thumbnail = MakeShared<FAssetThumbnail>(AssetData, ThumbnailResolution, ThumbnailResolution, ThumbnailPool);

	if (Thumbnail.IsValid())
	{
		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = true;
		return Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
	}

	return SNullWidget::NullWidget;
}
