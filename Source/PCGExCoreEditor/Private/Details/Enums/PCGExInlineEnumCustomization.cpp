// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"

namespace PCGExEnumCustomization
{
	TSharedRef<SWidget> CreateRadioGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), i)) { continue; }
			const FString KeyName = Enum->GetNameStringByIndex(i);

			FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);
			if (IconName.IsEmpty())
			{
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.Text(Enum->GetDisplayNameTextByIndex(i))
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonColorAndOpacity_Lambda(
						[PropertyHandle, KeyName]
						{
							FString CurrentValue;
							PropertyHandle->GetValueAsFormattedString(CurrentValue);
							return CurrentValue == KeyName ? FLinearColor(0.005, 0.005, 0.005, 0.8) : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(
						[PropertyHandle, KeyName]()
						{
							PropertyHandle->SetValueFromFormattedString(KeyName);
							return FReply::Handled();
						})
				];
			}
			else
			{
				IconName = TEXT("PCGEx.ActionIcon.") + IconName;
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
					.ButtonColorAndOpacity_Lambda(
						[PropertyHandle, KeyName]
						{
							FString CurrentValue;
							PropertyHandle->GetValueAsFormattedString(CurrentValue);
							return CurrentValue == KeyName ? FLinearColor(0.005, 0.005, 0.005, 0.8) : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(
						[PropertyHandle, KeyName]()
						{
							PropertyHandle->SetValueFromFormattedString(KeyName);
							return FReply::Handled();
						})
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush(*IconName))
						.ColorAndOpacity_Lambda(
							[PropertyHandle, Enum, i]
							{
								FString CurrentValue;
								PropertyHandle->GetValueAsFormattedString(CurrentValue);
								const FString KeyName = Enum->GetNameStringByIndex(i);
								return (CurrentValue == KeyName)
									       ? FLinearColor::White
									       : FLinearColor::Gray;
							})
					]
				];
			}
		}

		return Box;
	}

	TSharedRef<SWidget> CreateRadioGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum)
	{
		return CreateRadioGroup(PropertyHandle, FindFirstObjectSafe<UEnum>(*Enum));
	}

	TSharedRef<SWidget> CreateRadioGroup(UEnum* Enum, TFunction<int32()> GetValue, TFunction<void(int32)> SetValue)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), i)) { continue; }
			const int32 EnumValue = static_cast<int32>(Enum->GetValueByIndex(i));

			FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);
			if (IconName.IsEmpty())
			{
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.Text(Enum->GetDisplayNameTextByIndex(i))
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonColorAndOpacity_Lambda(
						[GetValue, EnumValue]
						{
							return GetValue() == EnumValue ? FLinearColor(0.005, 0.005, 0.005, 0.8) : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(
						[SetValue, EnumValue]()
						{
							SetValue(EnumValue);
							return FReply::Handled();
						})
				];
			}
			else
			{
				IconName = TEXT("PCGEx.ActionIcon.") + IconName;
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
					.ButtonColorAndOpacity_Lambda(
						[GetValue, EnumValue]
						{
							return GetValue() == EnumValue ? FLinearColor(0.005, 0.005, 0.005, 0.8) : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(
						[SetValue, EnumValue]()
						{
							SetValue(EnumValue);
							return FReply::Handled();
						})
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush(*IconName))
						.ColorAndOpacity_Lambda(
							[GetValue, EnumValue]
							{
								return GetValue() == EnumValue
									       ? FLinearColor::White
									       : FLinearColor::Gray;
							})
					]
				];
			}
		}

		return Box;
	}

	TSharedRef<SWidget> CreateCheckboxGroup(TSharedPtr<IPropertyHandle> PropertyHandle, UEnum* Enum, const TSet<int32>& SkipIndices)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), i) || SkipIndices.Contains(i)) { continue; }

			const FString KeyName = Enum->GetNameStringByIndex(i);
			const uint8 Bit = Enum->GetValueByIndex(i); // or (1LL << i)

			auto IsActive = [PropertyHandle, Bit]() -> bool
			{
				uint8 Mask = 0;
				if (PropertyHandle->GetValue(Mask) == FPropertyAccess::Success) { return (Mask & Bit) != 0; }
				return false;
			};

			auto Toggle = [PropertyHandle, Bit]()
			{
				uint8 Mask = 0;
				if (PropertyHandle->GetValue(Mask) == FPropertyAccess::Success)
				{
					Mask ^= Bit;
					PropertyHandle->SetValue(Mask);
					PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
				}
				return FReply::Handled();
			};

			FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);

			if (IconName.IsEmpty())
			{
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.Text(Enum->GetDisplayNameTextByIndex(i))
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonColorAndOpacity_Lambda(
						[IsActive]
						{
							return IsActive()
								       ? FLinearColor(0.005, 0.005, 0.005, 0.8)
								       : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(Toggle)
				];
			}
			else
			{
				IconName = TEXT("PCGEx.ActionIcon.") + IconName;

				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
					.ButtonColorAndOpacity_Lambda(
						[IsActive]
						{
							return IsActive()
								       ? FLinearColor(0.005, 0.005, 0.005, 0.8)
								       : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(Toggle)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush(*IconName))
						.ColorAndOpacity_Lambda(
							[IsActive]
							{
								return IsActive()
									       ? FLinearColor::White
									       : FLinearColor::Gray;
							})
					]
				];
			}
		}

		return Box;
	}

	TSharedRef<SWidget> CreateCheckboxGroup(const TSharedPtr<IPropertyHandle>& PropertyHandle, const FString& Enum, const TSet<int32>& SkipIndices)
	{
		return CreateCheckboxGroup(PropertyHandle, FindFirstObjectSafe<UEnum>(*Enum), SkipIndices);
	}

	TSharedRef<SWidget> CreateCheckboxGroup(UEnum* Enum, TFunction<uint8()> GetValue, TFunction<void(uint8)> SetValue, const TSet<int32>& SkipIndices)
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), i) || SkipIndices.Contains(i)) { continue; }

			const uint8 Bit = static_cast<uint8>(Enum->GetValueByIndex(i));

			auto IsActive = [GetValue, Bit]() -> bool
			{
				return (GetValue() & Bit) != 0;
			};

			auto Toggle = [GetValue, SetValue, Bit]()
			{
				SetValue(GetValue() ^ Bit);
				return FReply::Handled();
			};

			FString IconName = Enum->GetMetaData(TEXT("ActionIcon"), i);

			if (IconName.IsEmpty())
			{
				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.Text(Enum->GetDisplayNameTextByIndex(i))
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonColorAndOpacity_Lambda(
						[IsActive]
						{
							return IsActive()
								       ? FLinearColor(0.005, 0.005, 0.005, 0.8)
								       : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(Toggle)
				];
			}
			else
			{
				IconName = TEXT("PCGEx.ActionIcon.") + IconName;

				Box->AddSlot().AutoWidth().Padding(2, 2)
				[
					SNew(SButton)
					.ToolTipText(Enum->GetToolTipTextByIndex(i))
					.ButtonStyle(FAppStyle::Get(), "PCGEx.ActionIcon")
					.ButtonColorAndOpacity_Lambda(
						[IsActive]
						{
							return IsActive()
								       ? FLinearColor(0.005, 0.005, 0.005, 0.8)
								       : FLinearColor::Transparent;
						})
					.OnClicked_Lambda(Toggle)
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush(*IconName))
						.ColorAndOpacity_Lambda(
							[IsActive]
							{
								return IsActive()
									       ? FLinearColor::White
									       : FLinearColor::Gray;
							})
					]
				];
			}
		}

		return Box;
	}
}

FPCGExInlineEnumCustomization::FPCGExInlineEnumCustomization(const FString& InEnumName)
	: EnumName(InEnumName)
{
}

void FPCGExInlineEnumCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	UEnum* Enum = FindFirstObjectSafe<UEnum>(*EnumName);
	if (!Enum) { return; }

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
					PCGExEnumCustomization::CreateRadioGroup(PropertyHandle, Enum)
				]
			]
		];
}

void FPCGExInlineEnumCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}
