// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPackedFloatSlotCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PCGExPropertyFloatPacker.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Details/PCGExDetailRowWidgets.h"

#define LOCTEXT_NAMESPACE "PCGExPackedFloatSlotCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGExPackedFloatSlotCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPackedFloatSlotCustomization());
}

void FPCGExPackedFloatSlotCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> EnabledHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, bEnabled));
	TSharedPtr<IPropertyHandle> PropertyNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, PropertyName));
	TSharedPtr<IPropertyHandle> OffsetHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, Offset));

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

	// Value widget: the float offset, empty while unpinned with a greyed "auto" hint -- matching
	// the -1 sentinel, which means "append after the previous slot".
	auto GetOffsetText = [OffsetHandle]()
	{
		int32 Value = -1;
		if (OffsetHandle)
		{
			OffsetHandle->GetValue(Value);
		}
		// FromInt, not AsNumber: a float index must not pick up locale grouping separators.
		return Value < 0 ? FText::GetEmpty() : FText::FromString(FString::FromInt(Value));
	};

	auto GetHintText = [OffsetHandle]()
	{
		int32 Value = -1;
		if (OffsetHandle)
		{
			OffsetHandle->GetValue(Value);
		}
		return Value < 0 ? LOCTEXT("AutoOffsetHint", "auto") : FText::GetEmpty();
	};

	auto OnOffsetCommitted = [OffsetHandle](const FText& InText, ETextCommit::Type)
	{
		if (!OffsetHandle)
		{
			return;
		}

		const FString Trimmed = InText.ToString().TrimStartAndEnd();

		// Anything that isn't a usable index falls back to auto, so clearing the box is the
		// gesture for unpinning.
		int32 NewValue = -1;
		if (!Trimmed.IsEmpty() && Trimmed.IsNumeric())
		{
			NewValue = FCString::Atoi(*Trimmed);
			if (NewValue < 0)
			{
				NewValue = -1;
			}
		}

		OffsetHandle->SetValue(NewValue);
	};

	HeaderRow.ValueContent()
	         .MinDesiredWidth(120)
	[
		PCGExDetailRowWidgets::MakeArrowedHintTextBox(
			TAttribute<FText>::CreateLambda(GetOffsetText),
			TAttribute<FText>::CreateLambda(GetHintText),
			FOnTextCommitted::CreateLambda(OnOffsetCommitted),
			TAttribute<bool>::CreateLambda(IsEnabled))
	];
}

void FPCGExPackedFloatSlotCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Every current field is consumed by the header; anything added later still surfaces as a
	// regular child row rather than silently disappearing.
	TSharedPtr<IPropertyHandle> EnabledHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, bEnabled));
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
		if (ChildName == GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, bEnabled) ||
			ChildName == GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, PropertyName) ||
			ChildName == GET_MEMBER_NAME_CHECKED(FPCGExPackedFloatSlot, Offset))
		{
			continue;
		}

		IDetailPropertyRow& Row = ChildBuilder.AddProperty(Child.ToSharedRef());
		Row.IsEnabled(EnabledAttr);
	}
}

#undef LOCTEXT_NAMESPACE
