// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Bitmask/PCGExBitmaskEntryCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Data/Bitmasks/PCGExBitmaskCollection.h"
#include "Details/PCGExCustomizationMacros.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExBitmaskEntryCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExBitmaskEntryCustomization());
}

void FPCGExBitmaskEntryCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> IdentifierHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExBitmaskCollectionEntry, Identifier));
	TSharedPtr<IPropertyHandle> DirectionHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExBitmaskCollectionEntry, Direction));

	HeaderRow
		.NameContent()
		.MinDesiredWidth(200)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(1)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("ID :"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
			]
			+ SHorizontalBox::Slot()
			.Padding(1)
			.MinWidth(200)
			.VAlign(VAlign_Center)
			[
				IdentifierHandle->CreatePropertyValueWidget()
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			PCGEX_VECTORINPUTBOX(DirectionHandle)
		];
}

void FPCGExBitmaskEntryCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Handle = PropertyHandle->GetChildHandle(i);
		if (Handle->GetProperty()->GetFName() == FName("Identifier") ||
			Handle->GetProperty()->GetFName() == FName("Direction"))
		{
			continue;
		}

		ChildBuilder.AddProperty(Handle.ToSharedRef()).ShouldAutoExpand(true);
	}
}
