// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/InputSettings/PCGExCompareShorthandsCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Details/PCGExCustomizationMacros.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExCompareShorthandCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExCompareShorthandCustomization());
}

void FPCGExCompareShorthandCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Get handles
	TSharedPtr<IPropertyHandle> ComparisonHandle = PropertyHandle->GetChildHandle(FName("Comparison"));
	TSharedPtr<IPropertyHandle> InputHandle = PropertyHandle->GetChildHandle(FName("Input"));
	TSharedPtr<IPropertyHandle> ConstantHandle = PropertyHandle->GetChildHandle(FName("Constant"));
	TSharedPtr<IPropertyHandle> AttributeHandle = PropertyHandle->GetChildHandle(FName("Attribute"));
	TSharedPtr<IPropertyHandle> ToleranceHandle = PropertyHandle->GetChildHandle(FName("Tolerance"));
	TSharedPtr<IPropertyHandle> CleanupHandle = PropertyHandle->GetChildHandle(FName("bCleanupAttribute"));

	HeaderRow.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).MinWidth(30)
			[
				ComparisonHandle->CreatePropertyValueWidget()
			]
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
		]
		.ValueContent()
		.MinDesiredWidth(400)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
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
					CreateValueWidget(ConstantHandle)
				]
			]
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
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
					CreateAttributeWidget(AttributeHandle)
				]
			]
			+ SHorizontalBox::Slot().Padding(1)
			[
				SNew(SHorizontalBox)
				.Visibility(
					MakeAttributeLambda(
						[ComparisonHandle]()
						{
							uint8 V = 0;
							ComparisonHandle->GetValue(V);
							return V < 6 ? EVisibility::Collapsed : EVisibility::Visible;
						}))
				+ SHorizontalBox::Slot().Padding(1).AutoWidth()
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("±"))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)
				]
				+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
				[
					ToleranceHandle->CreatePropertyValueWidget()
				]
			]
			+ SHorizontalBox::Slot().Padding(1).AutoWidth().VAlign(VAlign_Center)
			[
				PCGExInputShorthandsCustomization::CreateOptionsPopover(InputHandle, CleanupHandle)
			]
		];
}

TSharedRef<IPropertyTypeCustomization> FPCGExCompareShorthandVectorCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExCompareShorthandVectorCustomization());
}

TSharedRef<SWidget> FPCGExCompareShorthandVectorCustomization::CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle)
{
	return PCGEX_VECTORINPUTBOX(ValueHandle);
}

TSharedRef<IPropertyTypeCustomization> FPCGExCompareShorthandRotatorCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExCompareShorthandRotatorCustomization());
}

TSharedRef<SWidget> FPCGExCompareShorthandRotatorCustomization::CreateValueWidget(TSharedPtr<IPropertyHandle> ValueHandle)
{
	return PCGEX_ROTATORINPUTBOX(ValueHandle);
}
