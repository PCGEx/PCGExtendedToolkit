// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExWeightedPropertyOverridesCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "PCGExProperty.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExWeightedPropertyOverridesCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExWeightedPropertyOverridesCustomization());
}

void FPCGExWeightedPropertyOverridesCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();
	PropertyHandlePtr = PropertyHandle;

	TSharedPtr<IPropertyHandle> WeightHandle = PropertyHandle->GetChildHandle(TEXT("Weight"));

	HeaderRow
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Weight")))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				WeightHandle.IsValid() ? WeightHandle->CreatePropertyValueWidget() : SNullWidget::NullWidget
			]
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExWeightedPropertyOverridesCustomization::GetHeaderText)))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
}

FText FPCGExWeightedPropertyOverridesCustomization::GetHeaderText() const
{
	if (!PropertyHandlePtr.IsValid()) { return FText::FromString(TEXT("0 / 0 active")); }

	TArray<void*> RawData;
	PropertyHandlePtr.Pin()->AccessRawData(RawData);
	int32 EnabledCount = 0;
	int32 TotalCount = 0;
	if (!RawData.IsEmpty() && RawData[0])
	{
		const FPCGExWeightedPropertyOverrides* OverridesStruct = static_cast<FPCGExWeightedPropertyOverrides*>(RawData[0]);
		TotalCount = OverridesStruct->Overrides.Num();
		EnabledCount = OverridesStruct->GetEnabledCount();
	}

	return FText::FromString(FString::Printf(TEXT("%d / %d active"), EnabledCount, TotalCount));
}

void FPCGExWeightedPropertyOverridesCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Get the inherited Overrides array handle
	TSharedPtr<IPropertyHandle> OverridesArrayHandle = PropertyHandle->GetChildHandle(TEXT("Overrides"));
	if (!OverridesArrayHandle.IsValid()) { return; }

	// Structural changes (add/remove/reorder) need immediate refresh.
	auto RefreshDelegate = FSimpleDelegate::CreateLambda([this]()
	{
		if (TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakPropertyUtilities.Pin())
		{
			PropertyUtilities->ForceRefresh();
		}
	});

	OverridesArrayHandle->SetOnPropertyValueChanged(RefreshDelegate);
	// No SetOnChildPropertyValueChanged — see FPCGExPropertyOverridesCustomization for rationale.

	// Iterate override entries - hide array controls
	uint32 NumElements = 0;
	OverridesArrayHandle->GetNumChildren(NumElements);

	for (uint32 i = 0; i < NumElements; ++i)
	{
		TSharedPtr<IPropertyHandle> ElementHandle = OverridesArrayHandle->GetChildHandle(i);
		if (ElementHandle.IsValid())
		{
			// Each entry uses FPCGExPropertyOverrideEntryCustomization automatically
			IDetailPropertyRow& Row = ChildBuilder.AddProperty(ElementHandle.ToSharedRef());
			Row.ShowPropertyButtons(false);
		}
	}
}
