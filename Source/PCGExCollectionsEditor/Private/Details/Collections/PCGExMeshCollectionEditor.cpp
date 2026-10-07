// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExMeshCollectionEditor.h"
#include "Details/Collections/PCGExCollectionEditorMacros.h"

#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"

#include "Collections/PCGExMeshCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Engine/StaticMesh.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"

FPCGExMeshCollectionEditor::FPCGExMeshCollectionEditor()
	: FPCGExAssetCollectionEditor()
{
}

void FPCGExMeshCollectionEditor::RegisterPushOptions(TArray<PCGExAssetCollectionEditor::FPushOption>& OutOptions)
{
	FPCGExAssetCollectionEditor::RegisterPushOptions(OutOptions);

#define PCGEX_DECL_PUSH_OPTION(_ID, _LABEL, _TOOLTIP, _GATE, ...) \
{\
	PCGExAssetCollectionEditor::FPushOption& Option = OutOptions.Emplace_GetRef();\
	Option.Id = FName(_ID);\
	Option.Label = FTEXT(_LABEL);\
	Option.Tooltip = FTEXT(_TOOLTIP);\
	Option.bRespectEnabledGate = _GATE;\
	Option.EntryPropertyNames = { __VA_ARGS__ };\
}

	PCGEX_DECL_PUSH_OPTION(
		"AssetEditor.Push.MaterialVariants",
		"Material Variants",
		"Push material variants configuration from the active entry to other selected entries.",
		false,
		FName("MaterialVariants"), FName("SlotIndex"),
		FName("MaterialOverrideVariants"), FName("MaterialOverrideVariantsList"))

	PCGEX_DECL_PUSH_OPTION(
		"AssetEditor.Push.Descriptors",
		"Descriptors",
		"Push descriptor source and ISM/SM descriptors from the active entry to other selected entries.",
		false,
		FName("DescriptorSource"), FName("ISMDescriptor"), FName("SMDescriptor"))

#undef PCGEX_DECL_PUSH_OPTION
}

void FPCGExMeshCollectionEditor::BuildAssetHeaderToolbar(FToolBarBuilder& ToolbarBuilder)
{
	FPCGExAssetCollectionEditor::BuildAssetHeaderToolbar(ToolbarBuilder);

#define PCGEX_CURRENT_COLLECTION if (UPCGExMeshCollection* Collection = Cast<UPCGExMeshCollection>(EditedCollection.Get()))

#pragma region Collision

	ToolbarBuilder.BeginSection("MeshToolsSection");
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

const UClass* FPCGExMeshCollectionEditor::GetTilePickerAllowedClass() const
{
	return UStaticMesh::StaticClass();
}
