// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyOverridesCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PCGExProperty.h"
#include "PropertyHandle.h"
#include "Delegates/Delegate.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyOverridesCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExPropertyOverridesCustomization());
}

void FPCGExPropertyOverridesCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Store utilities for refresh
	WeakPropertyUtilities = CustomizationUtils.GetPropertyUtilities();

	// Store property handle for dynamic text
	PropertyHandlePtr = PropertyHandle;

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FPCGExPropertyOverridesCustomization::GetHeaderText)))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
		];
}

FText FPCGExPropertyOverridesCustomization::GetHeaderText() const
{
	// IsValidHandle: AccessRawData dereferences the handle's property node unchecked.
	const TSharedPtr<IPropertyHandle> Handle = PropertyHandlePtr.Pin();
	if (!Handle.IsValid() || !Handle->IsValidHandle())
	{
		return FText::FromString(TEXT("0 / 0 active"));
	}

	TArray<void*> RawData;
	Handle->AccessRawData(RawData);
	int32 EnabledCount = 0;
	int32 TotalCount = 0;
	if (!RawData.IsEmpty() && RawData[0])
	{
		const FPCGExPropertyOverrides* OverridesStruct = static_cast<FPCGExPropertyOverrides*>(RawData[0]);
		TotalCount = OverridesStruct->Overrides.Num();
		EnabledCount = OverridesStruct->GetEnabledCount();
	}

	return FText::FromString(FString::Printf(TEXT("%d / %d active"), EnabledCount, TotalCount));
}

void FPCGExPropertyOverridesCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> OverridesArrayHandle = PropertyHandle->GetChildHandle(TEXT("Overrides"));
	if (!OverridesArrayHandle.IsValid())
	{
		return;
	}

	// Structural changes (add / remove / reorder) rebuild the rows, deferred to the next tick: the notification
	// can fire from inside a Slate event handler, where a synchronous ForceRefresh tears down live widgets.
	OverridesArrayHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities = WeakPropertyUtilities]()
	{
		if (const TSharedPtr<IPropertyUtilities> PropertyUtilities = WeakUtilities.Pin())
		{
			PropertyUtilities->RequestForceRefresh();
		}
	}));

	// IMPORTANT: Do NOT SetOnChildPropertyValueChanged with ForceRefresh here.
	// ForceRefresh() during the notification chain invalidates property nodes
	// before OnFinishedChangingProperties fires, which prevents the grid view's
	// multi-edit propagation from receiving valid property references.
	// Header text and enabled state use TAttribute getters that auto-update.

	// Hide array controls (add/remove/reorder buttons) - manually iterate instead
	uint32 NumElements = 0;
	OverridesArrayHandle->GetNumChildren(NumElements);

	for (uint32 i = 0; i < NumElements; ++i)
	{
		TSharedPtr<IPropertyHandle> ElementHandle = OverridesArrayHandle->GetChildHandle(i);
		if (ElementHandle.IsValid())
		{
			// Add each entry - FPCGExPropertyOverrideEntryCustomization will handle display
			IDetailPropertyRow& Row = ChildBuilder.AddProperty(ElementHandle.ToSharedRef());
			Row.ShowPropertyButtons(false); // Hide reset/browse buttons
		}
	}
}
