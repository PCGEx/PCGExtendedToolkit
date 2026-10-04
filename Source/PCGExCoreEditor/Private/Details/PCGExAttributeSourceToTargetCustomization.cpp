// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExAttributeSourceToTargetCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/PCGExAttributesDetails.h"
#include "Details/PCGExRawPropertyEdit.h"
#include "Styling/SlateColor.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PCGExAttributeSourceToTargetCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGExAttributeSourceToTargetCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExAttributeSourceToTargetCustomization());
}

void FPCGExAttributeSourceToTargetCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> SourceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExAttributeSourceToTargetDetails, Source));

	if (!SourceHandle.IsValid())
	{
		// Fall back to default widgets if the struct layout changes.
		HeaderRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()]
		         .ValueContent()[PropertyHandle->CreatePropertyValueWidget()];
		return;
	}

	// Read current state straight from the struct data so the InlineEditConditionToggle + gated Target stay in sync.
	// (Two separate child-handle SetValue calls race against the edit-condition refresh and the toggle write is lost.)
	auto GetDetails = [PropertyHandle]() -> const FPCGExAttributeSourceToTargetDetails*
	{
		void* RawData = nullptr;
		if (PropertyHandle->GetValueData(RawData) == FPropertyAccess::Success && RawData)
		{
			return static_cast<const FPCGExAttributeSourceToTargetDetails*>(RawData);
		}
		return nullptr;
	};

	HeaderRow.NameContent()
	         .MinDesiredWidth(220)
	[
		SourceHandle->CreatePropertyValueWidget()
	];

	// Value box edits Target. While not remapping (toggle off, or Target empty), it shows a greyed "= {Source}"
	// hint, mirroring GetOutputName()'s fallback to Source.
	auto GetTargetText = [GetDetails]()
	{
		const FPCGExAttributeSourceToTargetDetails* Details = GetDetails();
		if (!Details || !Details->bOutputToDifferentName || Details->Target.IsNone()) { return FText::GetEmpty(); }
		return FText::FromName(Details->Target);
	};

	auto GetHintText = [GetDetails]()
	{
		const FPCGExAttributeSourceToTargetDetails* Details = GetDetails();
		if (!Details) { return FText::GetEmpty(); }
		if (Details->bOutputToDifferentName && !Details->Target.IsNone()) { return FText::GetEmpty(); }
		return Details->Source.IsNone() ? FText::GetEmpty() : FText::FromString(TEXT("= ") + Details->Source.ToString());
	};

	// Commit writes both members in one transaction so the toggle and Target never disagree: a non-empty target
	// enables the remap; clearing it disables it (so it never tries to rename to an empty name).
	auto OnTargetCommitted = [PropertyHandle](const FText& InText, ETextCommit::Type)
	{
		const FString Trimmed = InText.ToString().TrimStartAndEnd();
		const bool bHasTarget = !Trimmed.IsEmpty();
		const FName NewTarget = bHasTarget ? FName(*Trimmed) : NAME_None;

		FPCGExRawPropertyEdit(PropertyHandle).Commit(LOCTEXT("SetAttributeRemap", "Set Attribute Remap"), [bHasTarget, NewTarget](void* RawData)
		{
			FPCGExAttributeSourceToTargetDetails* Details = static_cast<FPCGExAttributeSourceToTargetDetails*>(RawData);
			Details->bOutputToDifferentName = bHasTarget;
			Details->Target = NewTarget;
		});
	};

	HeaderRow.ValueContent()
	         .MinDesiredWidth(220)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 4, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("\x2192"))) // -> arrow
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		]
		+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
		[
			SNew(SEditableTextBox)
			.Text_Lambda(GetTargetText)
			.HintText_Lambda(GetHintText)
			.OnTextCommitted_Lambda(OnTargetCommitted)
			.SelectAllTextWhenFocused(true)
			.ClearKeyboardFocusOnCommit(true)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];
}

void FPCGExAttributeSourceToTargetCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

#undef LOCTEXT_NAMESPACE
