// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/InputSettings/PCGExApplySamplingCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"

TSharedRef<IPropertyTypeCustomization> FPCGExApplySamplingCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExApplySamplingCustomization());
}

void FPCGExApplySamplingCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGExApplySamplingCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumElements = 0;
	PropertyHandle->GetNumChildren(NumElements);

	TSet<FName> InlineNames = {
		FName("TransformPosition"),
		FName("TransformRotation"),
		FName("TransformScale"),
		FName("LookAtPosition"),
		FName("LookAtRotation"),
		FName("LookAtScale"),
	};

	for (uint32 i = 0; i < NumElements; ++i)
	{
		TSharedPtr<IPropertyHandle> ElementHandle = PropertyHandle->GetChildHandle(i);
		FName ElementName = ElementHandle ? ElementHandle->GetProperty()->GetFName() : NAME_None;

		if (!InlineNames.Contains(ElementName))
		{
			ChildBuilder.AddProperty(ElementHandle.ToSharedRef());
		}
		else
		{
			FString EditCondition = ElementHandle->GetMetaData(TEXT("EditCondition"));
			TSharedPtr<IPropertyHandle> ConditionHandle =
				ElementHandle->GetParentHandle()->GetChildHandle(*EditCondition);

			ChildBuilder
				.AddCustomRow(FText::FromName(ElementName))
				.Visibility(TAttribute<EVisibility>::CreateLambda([ConditionHandle]()
				{
					bool bApply = false;
					if (ConditionHandle.IsValid())
					{
						ConditionHandle->GetValue(bApply);
					}
					return bApply ? EVisibility::Visible : EVisibility::Collapsed;
				}))
				.NameContent()
				[
					ElementHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				[
					PCGExEnumCustomization::CreateCheckboxGroup(ElementHandle, TEXT("EPCGExApplySampledComponentFlags"), {0})
				];
		}
	}
}
