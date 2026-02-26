// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/Collections/PCGExCollectionGrammarCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Core/PCGExAssetCollection.h"
#include "Details/Enums/PCGExInlineEnumCustomization.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define PCGEX_SMALL_LABEL(_TEXT) \
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(1, 0)\
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray)).MinDesiredWidth(10)]

#define PCGEX_SMALL_LABEL_COL(_TEXT, _COL) \
+ SVerticalBox::Slot().AutoHeight().VAlign(VAlign_Center).Padding(1,8,1,2)\
[SNew(STextBlock).Text(FText::FromString(TEXT(_TEXT))).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(_COL)).MinDesiredWidth(10)]

#define PCGEX_SEP_LABEL(_TEXT)\
+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0)\
[SNew(STextBlock).Text(FText::FromString(_TEXT)).Font(IDetailLayoutBuilder::GetDetailFont()).ColorAndOpacity(FSlateColor(FLinearColor::Gray))]

#define PCGEX_FIELD_BASE(_HANDLE, _TYPE, _PART, _TOOLTIP)\
+ SHorizontalBox::Slot().Padding(1)[\
SNew(SNumericEntryBox<double>).Value_Lambda([=]() -> TOptional<double>{_TYPE V; _HANDLE->GetValue(V); return V._PART;})\
.OnValueCommitted_Lambda([=](double NewVal, ETextCommit::Type){_TYPE V; _HANDLE->GetValue(V); V._PART = NewVal; _HANDLE->SetValue(V);})\
.ToolTipText(FString(_TOOLTIP).IsEmpty() ? _HANDLE->GetToolTipText() : FText::FromString(_TOOLTIP)).AllowSpin(true)

#define PCGEX_STEP_VISIBILITY(_HANDLE)\
.Visibility_Lambda([_HANDLE](){uint8 EnumValue = 0;\
if (_HANDLE->GetValue(EnumValue) == FPropertyAccess::Success){ return EnumValue ? EVisibility::Visible : EVisibility::Collapsed;}\
return EVisibility::Collapsed;})

#define PCGEX_FIELD(_HANDLE, _TYPE, _PART, _TOOLTIP) PCGEX_FIELD_BASE(_HANDLE, _TYPE, _PART, _TOOLTIP)]

TSharedRef<IPropertyTypeCustomization> FPCGExCollectionGrammarCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExCollectionGrammarCustomization());
}

void FPCGExCollectionGrammarCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> SymbolHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExCollectionGrammarDetails, Symbol));
	TSharedPtr<IPropertyHandle> ScaleModeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExCollectionGrammarDetails, ScaleMode));
	TSharedPtr<IPropertyHandle> SizeHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExCollectionGrammarDetails, SizeMode));
	TSharedPtr<IPropertyHandle> DebugColorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExCollectionGrammarDetails, DebugColor));

	TSharedPtr<IPropertyHandle> GrammarSourceHandle = nullptr;
	if (TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle()) { GrammarSourceHandle = ParentHandle->GetChildHandle(FName("GrammarSource")); }

	// Grab parent collection
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);

	const bool bIsGlobal = PropertyHandle->GetProperty()->GetFName().ToString().Contains(TEXT("Global"));

	if (UPCGExAssetCollection* Collection = !OuterObjects.IsEmpty() ? Cast<UPCGExAssetCollection>(OuterObjects[0]) : nullptr;
		Collection && !bIsGlobal)
	{
		HeaderRow.NameContent()[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(1).AutoWidth()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			+ SHorizontalBox::Slot().Padding(10, 0).FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.Text_Lambda(
					[Collection]()
					{
						return Collection->GlobalGrammarMode == EPCGExGlobalVariationRule::Overrule
							       ? FText::FromString(TEXT("··· Overruled"))
							       : FText::GetEmpty();
					})
				.ColorAndOpacity_Lambda(
					[Collection]()
					{
						return Collection->GlobalGrammarMode == EPCGExGlobalVariationRule::Overrule
							       ? FLinearColor(1.0f, 0.5f, 0.1f, 0.5)
							       : FLinearColor::Transparent;
					})
			]

		];
	}
	else
	{
		HeaderRow.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
	}

	auto IsLocalData = [GrammarSourceHandle]()
	{
		if (!GrammarSourceHandle) { return true; }
		uint8 EnumValue = 0;
		GrammarSourceHandle->GetValue(EnumValue);
		return !EnumValue;
	};

	HeaderRow.ValueContent()
	         .MinDesiredWidth(400)
	[
		SNew(SVerticalBox)

		// Row 1: Symbol + Scale mode
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			PCGEX_SMALL_LABEL("Symbol")
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.IsEnabled_Lambda([bIsGlobal]() { return !bIsGlobal; })
				[
					SymbolHandle->CreatePropertyValueWidget()
				]
			]
			// Scale mode
			+ SHorizontalBox::Slot().Padding(1).AutoWidth()
			[
				SNew(SBox)
				.IsEnabled_Lambda(IsLocalData)
				[
					PCGExEnumCustomization::CreateRadioGroup(ScaleModeHandle, TEXT("EPCGExGrammarScaleMode"))
				]
			]
		]

		// Row 2: Size + Debug color
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			PCGEX_SMALL_LABEL("Size")
			+ SHorizontalBox::Slot().Padding(1).FillWidth(1)
			[
				SNew(SBox)
				.IsEnabled_Lambda(IsLocalData)
				[
					SizeHandle->CreatePropertyValueWidget()
				]
			]
			// Debug color
			PCGEX_SMALL_LABEL("·· ")
			+ SHorizontalBox::Slot().Padding(1).MaxWidth(25)
			[
				SNew(SBox)
				.IsEnabled_Lambda(IsLocalData)
				[
					DebugColorHandle->CreatePropertyValueWidget()
				]
			]
		]
	];
}

void FPCGExCollectionGrammarCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

#undef PCGEX_SMALL_LABEL
#undef PCGEX_SEP_LABEL
#undef PCGEX_FIELD_BASE
#undef PCGEX_FIELD
#undef PCGEX_FIELD_D
#undef PCGEX_STEP_VISIBILITY
