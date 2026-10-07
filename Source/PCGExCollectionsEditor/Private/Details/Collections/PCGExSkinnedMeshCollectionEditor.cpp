// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExSkinnedMeshCollectionEditor.h"
#include "Details/Collections/PCGExCollectionEditorMacros.h"

#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"

#include "Collections/PCGExSkinnedMeshCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Engine/SkinnedAsset.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"

FPCGExSkinnedMeshCollectionEditor::FPCGExSkinnedMeshCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
}

void FPCGExSkinnedMeshCollectionEditor::BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder)
{
	FPCGExAssetCollectionEditor::BuildAssetHeaderToolbar(ToolbarBuilder);

#define PCGEX_CURRENT_COLLECTION if (UPCGExSkinnedMeshCollection* Collection = Cast<UPCGExSkinnedMeshCollection>(EditedCollection.Get()))

#pragma region Collision

	ToolbarBuilder.BeginSection("SkinnedMeshToolsSection");
	{
		ToolbarBuilder.AddWidget(
			SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get(), "SimpleComboButton")
			.HasDownArrow(false)
			.ContentPadding(FMargin(4, 4))
			.ToolTipText(INVTEXT("Collision tools\nBatch-edit collision settings."))
			.ButtonContent()
			[
				PCGEX_COMBOBOX_BUTTON_CONTENT("PhysicsAssetEditor.DisableCollisionAll")
			]
			.OnGetMenuContent_Lambda(
				[this]() -> TSharedRef<SWidget>
				{
					return
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(4)
							[
								SNew(SButton)
								.Text(INVTEXT("Disable All Collisions"))
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION
										{
											Collection->EDITOR_DisableCollisions();
										}
										return FReply::Handled();
									})
								.ToolTipText(INVTEXT("Disable collision on all assets within that collection."))
							];
				})
			);

		ToolbarBuilder.AddWidget(
			SNew(SComboButton)
			.ComboButtonStyle(&FAppStyle::Get(), "SimpleComboButton")
			.HasDownArrow(false)
			.ContentPadding(FMargin(4, 4))
			.ToolTipText(INVTEXT("Descriptor tools\nBatch-set descriptor source for all entries."))
			.ButtonContent()
			[
				PCGEX_COMBOBOX_BUTTON_CONTENT("PCGEx.ActionIcon.CollectionRule")
			]
			.OnGetMenuContent_Lambda(
				[this]() -> TSharedRef<SWidget>
				{
					return
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(4)
							[
								SNew(SButton)
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION
										{
											Collection->EDITOR_SetDescriptorSourceAll(EPCGExEntryVariationMode::Global);
										}
										return FReply::Handled();
									})
								.ToolTipText(INVTEXT("Set all entry Descriptor to \"Inherit from collection\".\nEach entry will inherit from the collection global descriptors.\nNOTE: Local settings are preserved, just hidden."))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0, 0, 6, 0)
									[
										SNew(SImage).Image(FAppStyle::Get().GetBrush("PCGEx.ActionIcon.CollectionRule"))
									]
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(INVTEXT("Inherit from Collection"))
									]
								]
							]
							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(4, 0, 4, 4)
							[
								SNew(SButton)
								.OnClicked_Lambda(
									[this]()
									{
										PCGEX_CURRENT_COLLECTION
										{
											Collection->EDITOR_SetDescriptorSourceAll(EPCGExEntryVariationMode::Local);
										}
										return FReply::Handled();
									})
								.ToolTipText(INVTEXT("Set all entry Descriptor to \"Local\".\nEach entry manages its own descriptors.\nNOTE: This will restore previous local settings."))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									.Padding(0, 0, 6, 0)
									[
										SNew(SImage).Image(FAppStyle::Get().GetBrush("PCGEx.ActionIcon.EntryRule"))
									]
									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock).Text(INVTEXT("Local per Entry"))
									]
								]
							];
				})
			);
	}
	ToolbarBuilder.EndSection();

#pragma endregion

#undef PCGEX_CURRENT_COLLECTION
}

const UClass* FPCGExSkinnedMeshCollectionEditor::GetTilePickerAllowedClass() const
{
	return USkinnedAsset::StaticClass();
}
