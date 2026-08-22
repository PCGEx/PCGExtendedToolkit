// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExObjectPropertyOverrideDescriptionCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/PCGExDetailRowWidgets.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "Metadata/PCGObjectPropertyOverride.h"

namespace PCGExObjectPropertyOverrideDescriptionCustomization
{
	// Bare property/attribute name: drops the '$' and '@domain.' qualifiers, skips reserved (@Last/@Source) and empty.
	FString DeriveTargetName(const TSharedPtr<IPropertyHandle>& InputSourceHandle)
	{
		if (!InputSourceHandle.IsValid())
		{
			return FString();
		}

		void* RawData = nullptr;
		if (InputSourceHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
		{
			return FString();
		}

		const FPCGAttributePropertySelector* Selector = static_cast<const FPCGAttributePropertySelector*>(RawData);
		FString Name = Selector->GetAttributePropertyString(/*bAddPropertyQualifier=*/false);

		if (Name.IsEmpty() || Name.StartsWith(TEXT("@")))
		{
			return FString();
		}
		return Name;
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExObjectPropertyOverrideDescriptionCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExObjectPropertyOverrideDescriptionCustomization());
}

void FPCGExObjectPropertyOverrideDescriptionCustomization::OnInputSourceChanged()
{
	if (!PropertyTargetHandle.IsValid())
	{
		return;
	}

	const FString NewDerived = PCGExObjectPropertyOverrideDescriptionCustomization::DeriveTargetName(InputSourceHandle);

	FString CurrentTarget;
	PropertyTargetHandle->GetValue(CurrentTarget);

	// Tracking holds while the target still matches the PRE-edit baseline (not NewDerived, so a hand-edited
	// target stops it). Advance the baseline only on an actual write, or a transient empty source (e.g. @Last) desyncs it.
	const bool bWasTracking = CurrentTarget.IsEmpty() || CurrentTarget.Equals(TrackedDerived, ESearchCase::CaseSensitive);

	if (bWasTracking && !NewDerived.IsEmpty())
	{
		PropertyTargetHandle->SetValue(NewDerived);
		TrackedDerived = NewDerived;
	}
}

void FPCGExObjectPropertyOverrideDescriptionCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	InputSourceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGObjectPropertyOverrideDescription, InputSource));
	PropertyTargetHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGObjectPropertyOverrideDescription, PropertyTarget));

	if (!InputSourceHandle.IsValid() || !PropertyTargetHandle.IsValid())
	{
		// Fall back to the default widgets if the struct layout changes.
		HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
			.ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
		return;
	}

	TrackedDerived = PCGExObjectPropertyOverrideDescriptionCustomization::DeriveTargetName(InputSourceHandle);

	InputSourceHandle->SetOnPropertyValueChanged(
		FSimpleDelegate::CreateSP(this, &FPCGExObjectPropertyOverrideDescriptionCustomization::OnInputSourceChanged));

	auto GetTargetText = [TargetHandle = PropertyTargetHandle]()
	{
		FString Value;
		if (TargetHandle.IsValid())
		{
			TargetHandle->GetValue(Value);
		}
		return FText::FromString(Value);
	};

	// Grey preview of the derived target while PropertyTarget is empty.
	auto GetTargetHint = [InputHandle = InputSourceHandle, TargetHandle = PropertyTargetHandle]()
	{
		FString Value;
		if (TargetHandle.IsValid())
		{
			TargetHandle->GetValue(Value);
		}
		if (!Value.IsEmpty())
		{
			return FText::GetEmpty();
		}

		const FString Derived = PCGExObjectPropertyOverrideDescriptionCustomization::DeriveTargetName(InputHandle);
		return Derived.IsEmpty() ? FText::GetEmpty() : FText::FromString(Derived);
	};

	auto OnTargetCommitted = [TargetHandle = PropertyTargetHandle](const FText& InText, ETextCommit::Type)
	{
		if (!TargetHandle.IsValid())
		{
			return;
		}
		TargetHandle->SetValue(InText.ToString().TrimStartAndEnd());
	};

	// Reuse PCG's selector widget; HAlign_Fill so it fills the (splitter-driven) name column, which defaults to Left.
	HeaderRow.NameContent()
	         .HAlign(HAlign_Fill)
	[
		InputSourceHandle->CreatePropertyValueWidgetWithCustomization(nullptr)
	];

	HeaderRow.ValueContent()
	         .MinDesiredWidth(220)
	[
		PCGExDetailRowWidgets::MakeArrowedHintTextBox(
			TAttribute<FText>::CreateLambda(GetTargetText),
			TAttribute<FText>::CreateLambda(GetTargetHint),
			FOnTextCommitted::CreateLambda(OnTargetCommitted))
	];
}

void FPCGExObjectPropertyOverrideDescriptionCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}
