// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Bitmask/PCGExBitmaskCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Misc/Attribute.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Filters/Points/PCGExBitmaskFilter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace PCGExBitmaskCustomization
{
	TSharedRef<SWidget> BitsGrid(TSharedPtr<IPropertyHandle> BitmaskHandle)
	{
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel);
		Grid->SetSlotPadding(FMargin(2, 2));

		for (int32 i = 0; i < 64; ++i)
		{
			const bool bSwitch = ((i + 8) / 16) % 2 != 0;
			Grid->AddSlot(i % 16, i / 16)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "PCGEx.Checkbox")
				.RenderOpacity(bSwitch ? 0.8 : 1)
				.IsChecked_Lambda(
					[BitmaskHandle, BitIndex = i]()
					{
						int64 Mask = 0;
						BitmaskHandle->GetValue(Mask);
						return (Mask & (1LL << BitIndex)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda(
					[BitmaskHandle, BitIndex = i](ECheckBoxState NewState)
					{
						int64 Mask = 0;
						BitmaskHandle->GetValue(Mask);

						const int64 Bit = (1LL << BitIndex);
						if (NewState == ECheckBoxState::Checked)
						{
							Mask |= Bit;
						}
						else
						{
							Mask &= ~Bit;
						}

						BitmaskHandle->SetValue(Mask);
					})
			];
		}

		return SNew(SScaleBox)
			.Stretch(EStretch::UserSpecified)
			.StretchDirection(EStretchDirection::DownOnly)
			.IgnoreInheritedScale(true)
			.HAlign(HAlign_Left)
			.UserSpecifiedScale(0.5)
			[
				Grid
			];
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExBitmaskCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExBitmaskCustomization());
}

void FPCGExBitmaskCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()];
}

void FPCGExBitmaskCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Handle = PropertyHandle->GetChildHandle(i);
		if (Handle->GetProperty()->GetFName() == FName("Mode"))
		{
			continue;
		}
		if (Handle->GetProperty()->GetFName() == FName("Bitmask"))
		{
			BuildGrid(PropertyHandle, ChildBuilder);
		}
		else
		{
			ChildBuilder.AddProperty(Handle.ToSharedRef());
		}
	}
}

void FPCGExBitmaskCustomization::BuildGrid(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder) const
{
	TSharedPtr<IPropertyHandle> ModeHandle = PropertyHandle->GetChildHandle(FName("Mode"));
	TSharedPtr<IPropertyHandle> BitmaskHandle = PropertyHandle->GetChildHandle(FName("Bitmask"));

	ChildBuilder
		.AddCustomRow(FText::FromString("Bitmask"))
		.NameContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(1)
			.AutoHeight()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Bitmask :"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
				]
				+ SHorizontalBox::Slot()
				.Padding(1)
				.MinWidth(100)
				.VAlign(VAlign_Center)
				[
					BitmaskHandle->CreatePropertyValueWidget()
				]
			]
			+ SVerticalBox::Slot()
			.Padding(1)
			.AutoHeight()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(1)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Mode :"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
				]
				+ SHorizontalBox::Slot()
				.Padding(1)
				.MinWidth(100)
				.VAlign(VAlign_Center)
				[
					PCGExEnumCustomization::CreateRadioGroup(ModeHandle, "EPCGExBitmaskMode")
				]
			]
		]
		.ValueContent()
		[
			PCGExBitmaskCustomization::BitsGrid(BitmaskHandle)
		];
}

TSharedRef<IPropertyTypeCustomization> FPCGExBitmaskWithOperationCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExBitmaskWithOperationCustomization());
}

TSharedRef<IPropertyTypeCustomization> FPCGExBitmaskFilterConfigCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExBitmaskFilterConfigCustomization());
}

void FPCGExBitmaskFilterConfigCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The Bitmask value now lives inside the BitmaskValue shorthand; render it via the bits grid and
	// let every other editable property fall through. Load-only _DEPRECATED stubs carry no CPF_Edit.
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Handle = PropertyHandle->GetChildHandle(i);
		if (!Handle.IsValid() || !Handle->GetProperty() || !Handle->GetProperty()->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		if (Handle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FPCGExBitmaskFilterConfig, BitmaskValue))
		{
			BuildGrid(PropertyHandle, ChildBuilder);
		}
		else
		{
			ChildBuilder.AddProperty(Handle.ToSharedRef());
		}
	}
}

void FPCGExBitmaskFilterConfigCustomization::BuildGrid(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder) const
{
	TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExBitmaskFilterConfig, BitmaskValue));
	if (!ValueHandle.IsValid())
	{
		return;
	}

	TSharedPtr<IPropertyHandle> InputHandle = ValueHandle->GetChildHandle(FName("Input"));
	TSharedPtr<IPropertyHandle> ConstantHandle = ValueHandle->GetChildHandle(FName("Constant"));
	TSharedPtr<IPropertyHandle> AttributeHandle = ValueHandle->GetChildHandle(FName("Attribute"));

	if (!InputHandle.IsValid() || !ConstantHandle.IsValid() || !AttributeHandle.IsValid())
	{
		return;
	}

	// Input mode toggle beside the label; the value slot swaps between the 64-bit grid (Constant)
	// and the attribute selector (Attribute). EPCGExInputValueType::Constant is the zero value.
	ChildBuilder
		.AddCustomRow(FText::FromString("Bitmask"))
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(1)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Bitmask :"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
			]
			+ SHorizontalBox::Slot()
			.Padding(1)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				PCGExEnumCustomization::CreateRadioGroup(InputHandle, "EPCGExInputValueType")
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(1)
			.FillWidth(1)
			[
				SNew(SBox)
				.Visibility(
					MakeAttributeLambda(
						[InputHandle]()
						{
							uint8 V = 0;
							InputHandle->GetValue(V);
							return V ? EVisibility::Collapsed : EVisibility::Visible;
						}))
				[
					PCGExBitmaskCustomization::BitsGrid(ConstantHandle)
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(1)
			.FillWidth(1)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.Visibility(
					MakeAttributeLambda(
						[InputHandle]()
						{
							uint8 V = 0;
							InputHandle->GetValue(V);
							return V ? EVisibility::Visible : EVisibility::Collapsed;
						}))
				[
					AttributeHandle->CreatePropertyValueWidget()
				]
			]
		];
}
