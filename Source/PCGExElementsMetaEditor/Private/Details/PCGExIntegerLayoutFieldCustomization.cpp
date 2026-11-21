// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExIntegerLayoutFieldCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Elements/PCGExSplitJoinInteger.h"
#include "Styling/SlateColor.h"
#include "Styling/StyleColors.h"
#include "Types/PCGExTypeOpsString.h"
#include "UObject/UnrealType.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExIntegerLayoutFieldCustomization"

namespace PCGExIntegerLayoutFieldCustomization
{
	// A packed integer is 64 bits at most.
	constexpr int32 MaxPackedBit = 63;

	// Bits the entry takes: the entries after it stack up from bit 0. False outside a TArray of the struct, or on mixed values.
	bool GetBitRange(const TSharedRef<IPropertyHandle>& InEntryHandle, int32& OutLow, int32& OutHigh)
	{
		if (!InEntryHandle->IsValidHandle())
		{
			return false;
		}

		const TSharedPtr<IPropertyHandle> ParentHandle = InEntryHandle->GetParentHandle();
		if (!ParentHandle.IsValid() || !ParentHandle->IsValidHandle())
		{
			return false;
		}

		// IPropertyHandle::AsArray also accepts fixed-size arrays, whose raw value is not a TArray.
		const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(ParentHandle->GetProperty());
		const FStructProperty* InnerProperty = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
		if (!InnerProperty || InnerProperty->Struct != FPCGExIntegerLayoutField::StaticStruct())
		{
			return false;
		}

		void* RawData = nullptr;
		if (ParentHandle->GetValueData(RawData) != FPropertyAccess::Success || !RawData)
		{
			return false;
		}

		const TArray<FPCGExIntegerLayoutField>& Layout = *static_cast<const TArray<FPCGExIntegerLayoutField>*>(RawData);
		const int32 Index = InEntryHandle->GetIndexInArray();
		if (!Layout.IsValidIndex(Index))
		{
			return false;
		}

		OutLow = PCGExSplitJoinInteger::GetTotalBits(MakeArrayView(Layout).RightChop(Index + 1));
		OutHigh = OutLow + Layout[Index].NumBits - 1;
		return true;
	}
}

TSharedRef<IPropertyTypeCustomization> FPCGExIntegerLayoutFieldCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExIntegerLayoutFieldCustomization());
}

void FPCGExIntegerLayoutFieldCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> NameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExIntegerLayoutField, Name));
	const TSharedPtr<IPropertyHandle> BitsHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExIntegerLayoutField, NumBits));

	if (!NameHandle.IsValid() || !BitsHandle.IsValid())
	{
		// Fall back to default widgets if the struct layout changes.
		HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
		         .ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
		return;
	}

	auto GetNameText = [NameHandle]()
	{
		FName Value = NAME_None;
		return NameHandle->GetValue(Value) == FPropertyAccess::Success && !Value.IsNone() ? FText::FromName(Value) : FText::GetEmpty();
	};

	// An unnamed entry only reserves its bits.
	auto GetNameHint = [NameHandle]()
	{
		FName Value = NAME_None;
		return NameHandle->GetValue(Value) == FPropertyAccess::MultipleValues
			       ? LOCTEXT("MultipleValues", "Multiple Values")
			       : LOCTEXT("Padding", "padding");
	};

	// A commit also fires when focus leaves the box: an unchanged name opens no transaction.
	auto OnNameCommitted = [NameHandle](const FText& InText, ETextCommit::Type)
	{
		const FName NewName = PCGExTypeOps::MakeNameChecked(InText.ToString().TrimStartAndEnd());

		FName Current = NAME_None;
		if (NameHandle->GetValue(Current) == FPropertyAccess::Success && Current.IsEqual(NewName, ENameCase::CaseSensitive))
		{
			return;
		}

		NameHandle->SetValue(NewName);
	};

	auto GetRangeText = [PropertyHandle]()
	{
		int32 Low = 0;
		int32 High = 0;
		if (!PCGExIntegerLayoutFieldCustomization::GetBitRange(PropertyHandle, Low, High))
		{
			return FText::GetEmpty();
		}

		// \x2013 is an en dash.
		return Low == High
			       ? FText::AsNumber(Low)
			       : FText::Format(INVTEXT("{0}\x2013{1}"), FText::AsNumber(High), FText::AsNumber(Low));
	};

	auto GetRangeColor = [PropertyHandle]()
	{
		int32 Low = 0;
		int32 High = 0;
		const bool bOverflows = PCGExIntegerLayoutFieldCustomization::GetBitRange(PropertyHandle, Low, High)
			&& High > PCGExIntegerLayoutFieldCustomization::MaxPackedBit;
		return bOverflows ? FStyleColors::Error : FSlateColor::UseSubduedForeground();
	};

	HeaderRow.NameContent()
	         .MinDesiredWidth(220)
	         .HAlign(HAlign_Fill)
	[
		SNew(SEditableTextBox)
		.Text_Lambda(GetNameText)
		.HintText_Lambda(GetNameHint)
		.OnTextCommitted_Lambda(OnNameCommitted)
		.IsReadOnly_Lambda([NameHandle]() { return !NameHandle->IsEditable(); })
		.SelectAllTextWhenFocused(true)
		.ClearKeyboardFocusOnCommit(true)
		.ToolTipText(NameHandle->GetToolTipText())
		.Font(IDetailLayoutBuilder::GetDetailFont())
	];

	HeaderRow.ValueContent()
	         .MinDesiredWidth(220)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(64)
			[
				BitsHandle->CreatePropertyValueWidget()
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 10, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Bits", "bits"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda(GetRangeText)
			.ColorAndOpacity_Lambda(GetRangeColor)
			.ToolTipText(LOCTEXT("BitRangeTooltip", "Bits this entry takes in the packed integer, bit 0 being the least significant. Red past bit 63."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];
}

void FPCGExIntegerLayoutFieldCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

#undef LOCTEXT_NAMESPACE
