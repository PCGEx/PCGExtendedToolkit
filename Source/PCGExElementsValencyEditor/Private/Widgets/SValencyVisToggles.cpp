// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Widgets/SValencyVisToggles.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#include "EditorMode/PCGExValencyCageEditorMode.h"
#include "Widgets/PCGExValencyWidgetHelpers.h"

namespace Style = PCGExValencyWidgets::Style;

void SValencyVisToggles::Construct(const FArguments& InArgs)
{
	EditorMode = InArgs._EditorMode;

	if (!EditorMode)
	{
		ChildSlot
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("PCGExValency", "NoMode", "No editor mode"))
		];
		return;
	}

	FValencyVisibilityFlags& Flags = EditorMode->GetMutableVisibilityFlags();
	FValencyVisibilityFlags* FlagsPtr = &Flags;

	ChildSlot
	[
		SNew(SComboButton)
		.ContentPadding(FMargin(4, 2))
		.HasDownArrow(true)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("PCGExValency", "VisTogglesLabel", "Visibility"))
				.Font(Style::Title())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([FlagsPtr]() -> FText
				{
					int32 Count = 0;
					if (FlagsPtr->bShowConnections) Count++;
					if (FlagsPtr->bShowLabels) Count++;
					if (FlagsPtr->bShowConnectors) Count++;
					if (FlagsPtr->bShowVolumes) Count++;
					if (FlagsPtr->bShowGhostMeshes) Count++;
					if (FlagsPtr->bShowPatterns) Count++;
					if (FlagsPtr->bShowConstraints) Count++;
					return FText::Format(NSLOCTEXT("PCGExValency", "VisCount", "({0}/7)"),
						FText::AsNumber(Count));
				})
				.Font(Style::Label())
				.ColorAndOpacity(Style::DimColor())
			]
		]
		.MenuContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "ToggleConnections", "Connections"),
					NSLOCTEXT("PCGExValency", "ToggleConnectionsTip", "Show orbital arrows and connection lines"),
					&Flags.bShowConnections)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "ToggleLabels", "Labels"),
					NSLOCTEXT("PCGExValency", "ToggleLabelsTip", "Show cage names and orbital labels"),
					&Flags.bShowLabels)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "ToggleConnectors", "Connectors"),
					NSLOCTEXT("PCGExValency", "ToggleConnectorsTip", "Show connector component diamonds"),
					&Flags.bShowConnectors)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "ToggleVolumes", "Volumes"),
					NSLOCTEXT("PCGExValency", "ToggleVolumesTip", "Show volume and palette wireframes"),
					&Flags.bShowVolumes)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "ToggleGhosts", "Ghosts"),
					NSLOCTEXT("PCGExValency", "ToggleGhostsTip", "Show mirror/proxy ghost meshes"),
					&Flags.bShowGhostMeshes)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "TogglePatterns", "Patterns"),
					NSLOCTEXT("PCGExValency", "TogglePatternsTip", "Show pattern bounds and proxy lines"),
					&Flags.bShowPatterns)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				MakeToggleButton(
					NSLOCTEXT("PCGExValency", "ToggleConstraints", "Constraints"),
					NSLOCTEXT("PCGExValency", "ToggleConstraintsTip", "Show connector constraint zones and indicators"),
					&Flags.bShowConstraints)
			]
		]
	];
}

TSharedRef<SWidget> SValencyVisToggles::MakeToggleButton(const FText& Label, const FText& Tooltip, bool* FlagPtr)
{
	return SNew(SCheckBox)
		.ToolTipText(Tooltip)
		.IsChecked_Lambda([FlagPtr]()
		{
			return *FlagPtr ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, FlagPtr](ECheckBoxState NewState)
		{
			*FlagPtr = (NewState == ECheckBoxState::Checked);
			RedrawViewports();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Style::Label())
		];
}

void SValencyVisToggles::RedrawViewports() const
{
	if (GEditor)
	{
		GEditor->RedrawAllViewports();
	}
}
