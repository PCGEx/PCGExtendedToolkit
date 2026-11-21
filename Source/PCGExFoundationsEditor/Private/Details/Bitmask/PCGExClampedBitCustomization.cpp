// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Bitmask/PCGExClampedBitCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExClampedBitCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExClampedBitCustomization());
}

void FPCGExClampedBitCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> BitIndexHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampedBit, BitIndex));
	TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampedBit, bValue));

	HeaderRow.NameContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("Bit : "))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.MinWidth(50)
		.Padding(2, 0)
		[
			BitIndexHandle->CreatePropertyValueWidget()
		]
	].ValueContent()
	[
		ValueHandle->CreatePropertyValueWidget()
	];
}

void FPCGExClampedBitCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}
