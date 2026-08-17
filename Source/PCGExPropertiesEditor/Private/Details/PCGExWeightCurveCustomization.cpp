// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExWeightCurveCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Utils/PCGExWeightCurve.h"

#include "Widgets/SPCGExPropertyCurveValueWidget.h"

#define LOCTEXT_NAMESPACE "PCGExWeightCurveCustomization"

TSharedRef<IPropertyTypeCustomization> FPCGExWeightCurveCustomization::MakeInstance()
{
	return MakeShared<FPCGExWeightCurveCustomization>();
}

void FPCGExWeightCurveCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	CurveHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FPCGExWeightCurve, Curve));

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

void FPCGExWeightCurveCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (!CurveHandle.IsValid())
	{
		return;
	}

	// Clamps come from the HOST property's metas -- the wrapper struct itself stays clamp-agnostic.
	ChildBuilder.AddCustomRow(LOCTEXT("CurveEditorRow", "Curve Editor"))
		.WholeRowContent()
		[
			SNew(SPCGExPropertyCurveValueWidget, CurveHandle.ToSharedRef())
			.Clamps(FPCGExPropertyCurveClamps::FromProperty(PropertyHandle->GetProperty()))
		];
}

#undef LOCTEXT_NAMESPACE
