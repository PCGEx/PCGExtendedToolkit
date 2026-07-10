// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyOutputConfigCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PCGExPropertyWriter.h"
#include "PropertyHandle.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Details/PCGExDetailRowWidgets.h"

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyOutputConfigCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertyOutputConfigCustomization());
}

void FPCGExPropertyOutputConfigCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> EnabledHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, bEnabled));
	TSharedPtr<IPropertyHandle> PropertyNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, PropertyName));
	TSharedPtr<IPropertyHandle> OutputAttributeNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, OutputAttributeName));

	auto IsEnabled = [EnabledHandle]()
	{
		bool bEnabled = false;
		if (EnabledHandle)
		{
			EnabledHandle->GetValue(bEnabled);
		}
		return bEnabled;
	};

	HeaderRow.NameContent()
	         .MinDesiredWidth(220)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 2, 0)
		[
			EnabledHandle->CreatePropertyValueWidget()
		]
		+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
		[
			SNew(SBox)
			.IsEnabled_Lambda(IsEnabled)
			[
				PropertyNameHandle->CreatePropertyValueWidget()
			]
		]
	];

	// Value widget: a text box that shows OutputAttributeName, with a greyed hint
	// "= {SanitizedPropertyName}" when OutputAttributeName is None (runtime falls back to
	// PropertyName - see FPCGExPropertyOutputConfig::GetEffectiveOutputName).
	auto GetOutputText = [OutputAttributeNameHandle]()
	{
		FName Value = NAME_None;
		if (OutputAttributeNameHandle)
		{
			OutputAttributeNameHandle->GetValue(Value);
		}
		return Value.IsNone() ? FText::GetEmpty() : FText::FromName(Value);
	};

	auto GetHintText = [OutputAttributeNameHandle, PropertyNameHandle]()
	{
		FName OutputValue = NAME_None;
		if (OutputAttributeNameHandle)
		{
			OutputAttributeNameHandle->GetValue(OutputValue);
		}
		if (!OutputValue.IsNone())
		{
			return FText::GetEmpty();
		}

		FName PropName = NAME_None;
		if (PropertyNameHandle)
		{
			PropertyNameHandle->GetValue(PropName);
		}
		const FName Sanitized = PCGExMetaHelpers::SanitizeAttributeName(PropName);
		if (Sanitized.IsNone())
		{
			return FText::GetEmpty();
		}
		return FText::FromString(TEXT("= ") + Sanitized.ToString());
	};

	auto OnOutputCommitted = [OutputAttributeNameHandle](const FText& InText, ETextCommit::Type)
	{
		if (!OutputAttributeNameHandle)
		{
			return;
		}
		const FString Trimmed = InText.ToString().TrimStartAndEnd();
		const FName NewValue = Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed);
		OutputAttributeNameHandle->SetValue(NewValue);
	};

	HeaderRow.ValueContent()
	         .MinDesiredWidth(220)
	[
		PCGExDetailRowWidgets::MakeArrowedHintTextBox(
			TAttribute<FText>::CreateLambda(GetOutputText),
			TAttribute<FText>::CreateLambda(GetHintText),
			FOnTextCommitted::CreateLambda(OnOutputCommitted),
			TAttribute<bool>::CreateLambda(IsEnabled))
	];
}

void FPCGExPropertyOutputConfigCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The base FPCGExPropertyOutputConfig consumes all its fields in the header row; derived
	// configs (e.g. FPCGExPropertySampledOutputConfig, registered on this same customization)
	// surface their extra fields as regular child rows, enabled-gated like the header widgets.
	TSharedPtr<IPropertyHandle> EnabledHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, bEnabled));
	const TAttribute<bool> EnabledAttr = TAttribute<bool>::CreateLambda(
		[EnabledHandle]()
		{
			bool bEnabled = false;
			if (EnabledHandle)
			{
				EnabledHandle->GetValue(bEnabled);
			}
			return bEnabled;
		});

	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(i);
		if (!Child.IsValid() || !Child->GetProperty())
		{
			continue;
		}

		const FName ChildName = Child->GetProperty()->GetFName();
		if (ChildName == GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, bEnabled) ||
			ChildName == GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, PropertyName) ||
			ChildName == GET_MEMBER_NAME_CHECKED(FPCGExPropertyOutputConfig, OutputAttributeName))
		{
			continue;
		}

		IDetailPropertyRow& Row = ChildBuilder.AddProperty(Child.ToSharedRef());
		Row.IsEnabled(EnabledAttr);
	}
}
