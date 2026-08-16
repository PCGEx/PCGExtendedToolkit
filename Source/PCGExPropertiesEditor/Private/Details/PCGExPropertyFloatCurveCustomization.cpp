// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExPropertyFloatCurveCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PCGExPropertyTypes.h"
#include "PropertyHandle.h"

#include "Widgets/SPCGExPropertyCurveValueWidget.h"

#define LOCTEXT_NAMESPACE "PCGExPropertyFloatCurveCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGExPropertyFloatCurveCustomization::MakeInstance()
{
	return MakeShared<FPCGExPropertyFloatCurveCustomization>();
}

void FPCGExPropertyFloatCurveCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExProperty_FloatCurve, Value));

	// The editor is a full-width child row (WholeRowContent, built in CustomizeChildren).
	// ShouldAutoExpand forces the struct open so the editor is always visible without a manual
	// expand, while still getting the full row width a struct header's value column can't provide.
	HeaderRow
		.ShouldAutoExpand(true)
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
}

void FPCGExPropertyFloatCurveCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (!ValueHandle.IsValid())
	{
		return;
	}

	ChildBuilder.AddCustomRow(LOCTEXT("CurveEditorRow", "Curve Editor"))
		.WholeRowContent()
		[
			SNew(SPCGExPropertyCurveValueWidget, ValueHandle.ToSharedRef())
		];
}

#pragma region FPCGExRuntimeFloatCurveCustomization

TSharedRef<IPropertyTypeCustomization> FPCGExRuntimeFloatCurveCustomization::MakeInstance()
{
	return MakeShared<FPCGExRuntimeFloatCurveCustomization>();
}

void FPCGExRuntimeFloatCurveCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Rows that fully replace their widget (AddCompactValueRow / AddComplexValueRows) never call
	// this; it serves any path that surfaces the raw curve with a default header.
	HeaderRow
		.ShouldAutoExpand(true)
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
}

void FPCGExRuntimeFloatCurveCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ChildBuilder.AddCustomRow(LOCTEXT("CurveEditorRow", "Curve Editor"))
		.WholeRowContent()
		[
			SNew(SPCGExPropertyCurveValueWidget, PropertyHandle)
		];
}

bool FPCGExPropertyOwnedCurveIdentifier::IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const
{
	const FProperty* Property = PropertyHandle.GetProperty();
	const UScriptStruct* OwnerStruct = Property ? Cast<UScriptStruct>(Property->GetOwnerStruct()) : nullptr;
	return OwnerStruct && OwnerStruct->IsChildOf(FPCGExProperty::StaticStruct());
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
