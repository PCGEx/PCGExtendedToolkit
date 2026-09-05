// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/InputSettings/PCGExClampDetailsCustomization.h"

#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Details/PCGExClampDetails.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExClampDetailsCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExClampDetailsCustomization());
}

void FPCGExClampDetailsCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> ApplyMinHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampDetails, bApplyClampMin));
	TSharedPtr<IPropertyHandle> ClampMinHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampDetails, ClampMinValue));
	TSharedPtr<IPropertyHandle> ApplyMaxHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampDetails, bApplyClampMax));
	TSharedPtr<IPropertyHandle> ClampMaxHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExClampDetails, ClampMaxValue));

	HeaderRow.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).AutoWidth()
			[
				ApplyMinHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot().Padding(1).MinSize(50)
			[
				ClampMinHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot().Padding(1).MinSize(50)
			[
				ClampMaxHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot().Padding(1).AutoWidth()
			[
				ApplyMaxHandle->CreatePropertyValueWidget()
			]
		];
}

void FPCGExClampDetailsCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}
