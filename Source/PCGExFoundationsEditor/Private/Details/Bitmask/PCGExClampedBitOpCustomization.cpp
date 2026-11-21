// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Bitmask/PCGExClampedBitOpCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Data/Bitmasks/PCGExBitmaskDetails.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExClampedBitOpCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExClampedBitOpCustomization());
}

void FPCGExClampedBitOpCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> BitIndexHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampedBitOp, BitIndex));
	TSharedPtr<IPropertyHandle> OpHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampedBitOp, Op));
	TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampedBitOp, bValue));

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
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.MinWidth(50)
		.Padding(2, 0)
		[
			OpHandle->CreatePropertyValueWidget()
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		.Padding(2, 0)
		[
			ValueHandle->CreatePropertyValueWidget()
		]
	];
}

void FPCGExClampedBitOpCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}
