// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/SPCGExCollectionFilterBar.h"

#include "PCGExCollectionsEditorSettings.h"
#include "Styling/AppStyle.h"
#include "Details/PCGExCategoryGroups.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

void SPCGExCollectionFilterBar::Construct(const FArguments& InArgs)
{
	Groups = InArgs._Groups;

	const TSharedRef<SUniformGridPanel> Grid =
		SNew(SUniformGridPanel)
		.SlotPadding(FMargin(2, 2));

	Grid->AddSlot(0, 0)
	[
		SNew(SButton)
		.Text(INVTEXT("Show all"))
		.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
		.OnClicked(this, &SPCGExCollectionFilterBar::SetAllHidden, false)
		.ToolTipText(INVTEXT("Turns all filters off and shows all properties."))
	];

	Grid->AddSlot(0, 1)
	[
		SNew(SButton)
		.Text(INVTEXT("Hide all"))
		.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
		.OnClicked(this, &SPCGExCollectionFilterBar::SetAllHidden, true)
		.ToolTipText(INVTEXT("Turns all filters on and hides all properties."))
	];

	int32 Index = 2;
	for (const PCGExCollectionCategoryGroups::FGroup& Group : Groups)
	{
		const FName Id = Group.Id;
		const FName ParentId = PCGExCategoryGroups::GetParentGroup(Id);

		Grid->AddSlot(Index / 2, Index % 2)
		[
			SNew(SButton)
			.OnClicked(this, &SPCGExCollectionFilterBar::ToggleGroup, Id)
			// A sub-group toggle is inert while its parent hides everything beneath it.
			.IsEnabled_Lambda([ParentId]
			{
				return ParentId.IsNone() || !PCGExCollectionCategoryGroups::IsGroupHidden(ParentId);
			})
			.ButtonColorAndOpacity_Lambda([Id]
			{
				return PCGExCollectionCategoryGroups::IsGroupHidden(Id) ? FLinearColor::Transparent : FLinearColor(0.005f, 0.005f, 0.005f, 0.5f);
			})
			.ToolTipText(Group.ToolTip)
			[
				SNew(STextBlock)
				.Text(Group.Label)
				.StrikeBrush_Lambda([Id]
				{
					return PCGExCollectionCategoryGroups::IsGroupHidden(Id) ? FAppStyle::GetBrush("Common.StrikeThrough") : nullptr;
				})
			]
		];

		Index++;
	}

	ChildSlot
	[
		Grid
	];
}

FReply SPCGExCollectionFilterBar::SetAllHidden(const bool bHidden)
{
	TArray<FName> Ids;
	Ids.Reserve(Groups.Num());
	for (const PCGExCollectionCategoryGroups::FGroup& Group : Groups)
	{
		Ids.Add(Group.Id);
	}

	GetMutableDefault<UPCGExCollectionsEditorSettings>()->SetCategoryGroupsHidden(Ids, bHidden);
	return FReply::Handled();
}

FReply SPCGExCollectionFilterBar::ToggleGroup(const FName Group)
{
	UPCGExCollectionsEditorSettings* Settings = GetMutableDefault<UPCGExCollectionsEditorSettings>();
	Settings->SetCategoryGroupHidden(Group, !Settings->HiddenCategoryGroups.Contains(Group));
	return FReply::Handled();
}
