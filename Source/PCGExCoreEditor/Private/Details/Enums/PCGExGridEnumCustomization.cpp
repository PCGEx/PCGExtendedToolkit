// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Enums/PCGExGridEnumCustomization.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

FPCGExGridEnumCustomization::FPCGExGridEnumCustomization(const FString& InEnumName, const int32 InColumns)
	: EnumName(InEnumName)
	  , Columns(InColumns)
{
}

void FPCGExGridEnumCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	EnumHandle = PropertyHandle;

	if (!PropertyHandle->HasMetaData("InlineEnum"))
	{
		HeaderRow
			.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
			.ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
		return;
	}

	UEnum* Enum = FindFirstObjectSafe<UEnum>(*EnumName);
	if (!Enum)
	{
		return;
	}

	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
		.ValueContent()
		.MaxDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SUniformGridPanel)
				+ SUniformGridPanel::Slot(0, 0)
				[
					GenerateEnumButtons(Enum)
				]
			]
		];
}

void FPCGExGridEnumCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

TSharedRef<SWidget> FPCGExGridEnumCustomization::GenerateEnumButtons(UEnum* Enum)
{
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel);
	Grid->SetSlotPadding(FMargin(2, 2));

	int32 ItemIndex = 0;

	for (const int32 i : PCGExEnumCustomization::GetEnumDisplayOrder(Enum))
	{
		if (Enum->HasMetaData(TEXT("Hidden"), i))
		{
			continue;
		}
		const FString KeyName = Enum->GetNameStringByIndex(i);

		FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);
		if (IconName.IsEmpty())
		{
			Grid->AddSlot(ItemIndex % Columns, ItemIndex / Columns)
			[
				SNew(SButton)
				.Text(Enum->GetDisplayNameTextByIndex(i))
				.ToolTipText(Enum->GetToolTipTextByIndex(i))
				.ButtonColorAndOpacity_Lambda(
					[this, KeyName]
					{
						FString CurrentValue;
						EnumHandle->GetValueAsFormattedString(CurrentValue);
						return CurrentValue == KeyName ? FLinearColor(0.005, 0.005, 0.005, 0.8) : FLinearColor::Transparent;
					})
				.OnClicked_Lambda(
					[this, KeyName]()
					{
						EnumHandle->SetValueFromFormattedString(KeyName);
						return FReply::Handled();
					})
			];
		}
		else
		{
			IconName = TEXT("PCGEx.ActionIcon.") + IconName;
			Grid->AddSlot(ItemIndex % Columns, ItemIndex / Columns)
			[
				SNew(SButton)
				.ToolTipText(Enum->GetToolTipTextByIndex(i))
				.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
				.ButtonColorAndOpacity_Lambda(
					[this, KeyName]
					{
						FString CurrentValue;
						EnumHandle->GetValueAsFormattedString(CurrentValue);
						return CurrentValue == KeyName ? FLinearColor(0.005, 0.005, 0.005, 0.8f) : FLinearColor::Transparent;
					})
				.OnClicked_Lambda(
					[this, KeyName]()
					{
						EnumHandle->SetValueFromFormattedString(KeyName);
						return FReply::Handled();
					})
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush(*IconName))
					.ColorAndOpacity_Lambda(
						[this, Enum, i]
						{
							FString CurrentValue;
							EnumHandle->GetValueAsFormattedString(CurrentValue);
							const FString KeyName = Enum->GetNameStringByIndex(i);
							return (CurrentValue == KeyName)
								? FLinearColor::White
								: FLinearColor::Gray;
						})
				]
			];
		}

		ItemIndex++;
	}

	return Grid;
}
