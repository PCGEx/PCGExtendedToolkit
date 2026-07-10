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

#undef LOCTEXT_NAMESPACE
