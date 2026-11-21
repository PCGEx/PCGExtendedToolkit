// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExSelectorRangeAxisCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Collections/PCGExCollectionEditorSlateUtils.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Selectors/PCGExSelectorRangeBased.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

using PCGExCollectionEditorSlateUtils::MakeSmallLabel;

TSharedRef<IPropertyTypeCustomization> FPCGExSelectorRangeAxisCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExSelectorRangeAxisCustomization());
}

void FPCGExSelectorRangeAxisCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> SourceModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExSelectorRangeAxis, SourceMode));
	const TSharedPtr<IPropertyHandle> MinNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExSelectorRangeAxis, MinPropertyName));
	const TSharedPtr<IPropertyHandle> MaxNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExSelectorRangeAxis, MaxPropertyName));
	const TSharedPtr<IPropertyHandle> RangeNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExSelectorRangeAxis, RangePropertyName));
	const TSharedPtr<IPropertyHandle> BoundaryModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExSelectorRangeAxis, BoundaryMode));

	HeaderRow
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				PCGExEnumCustomization::CreateCycleButton(SourceModeHandle, TEXT("EPCGExRangeSourceMode"))
			]
		]
		.ValueContent()
		.MinDesiredWidth(300)
		[
			SNew(SHorizontalBox)

			// Property fields -- switched on SourceMode. SWidgetSwitcher (rather than sibling slots
			// with toggled Visibility) keeps the inactive variant from claiming FillWidth.
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			.VAlign(VAlign_Center)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda(
					[SourceModeHandle]()
					{
						uint8 RawValue = 0;
						if (SourceModeHandle.IsValid() && SourceModeHandle->GetValue(RawValue) == FPropertyAccess::Success)
						{
							return static_cast<EPCGExRangeSourceMode>(RawValue) == EPCGExRangeSourceMode::Vector2 ? 1 : 0;
						}
						return 0;
					})

				// Slot 0 -- TwoNumerics: "min:[MinPropertyName] max:[MaxPropertyName]"
				+ SWidgetSwitcher::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)
					[
						MakeSmallLabel(TEXT("min:"))
					]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(1, 0)
					[
						MinNameHandle->CreatePropertyValueWidget()
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 1, 0)
					[
						MakeSmallLabel(TEXT("max:"))
					]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(1, 0)
					[
						MaxNameHandle->CreatePropertyValueWidget()
					]
				]

				// Slot 1 -- Vector2: "min:max [RangePropertyName]"
				+ SWidgetSwitcher::Slot()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)
					[
						MakeSmallLabel(TEXT("min:max"))
					]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(1, 0)
					[
						RangeNameHandle->CreatePropertyValueWidget()
					]
				]
			]

			// BoundaryMode -- compact cycle button trailing the property fields.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6, 0, 0, 0)
			[
				PCGExEnumCustomization::CreateCycleButton(BoundaryModeHandle, TEXT("EPCGExRangeBoundaryMode"))
			]
		];
}

void FPCGExSelectorRangeAxisCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The only child surfaced is ValueSource -- its registered FPCGExInputShorthandSelectorDouble
	// customization renders the shorthand row, untouched.
	const TSharedPtr<IPropertyHandle> ValueSourceHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExSelectorRangeAxis, ValueSource));
	if (ValueSourceHandle.IsValid())
	{
		ChildBuilder.AddProperty(ValueSourceHandle.ToSharedRef());
	}
}
