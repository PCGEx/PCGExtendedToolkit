// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExEdgeEndpointsCompareNumFilterConfigCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Widgets/SPCGExEdgeEndpointsCompareNumPreview.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExEdgeEndpointsCompareNumFilterConfigCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExEdgeEndpointsCompareNumFilterConfigCustomization());
}

void FPCGExEdgeEndpointsCompareNumFilterConfigCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ComparisonHandle = PropertyHandle->GetChildHandle(TEXT("Comparison"));
	ToleranceHandle = PropertyHandle->GetChildHandle(TEXT("Tolerance"));
	InvertHandle = PropertyHandle->GetChildHandle(TEXT("bInvert"));

	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGExEdgeEndpointsCompareNumFilterConfigCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// 1. Insert the preview widget as the first custom row
	ChildBuilder.AddCustomRow(FText::FromString(TEXT("Preview")))
		.WholeRowContent()
		[
			SNew(SBox)
			.HeightOverride(140.0f)
			[
				SAssignNew(PreviewWidget, SPCGExEdgeEndpointsCompareNumPreview)
				.Comparison_Lambda([this]() -> EPCGExComparison
				{
					uint8 Value = 0;
					if (ComparisonHandle.IsValid()) { ComparisonHandle->GetValue(Value); }
					return static_cast<EPCGExComparison>(Value);
				})
				.bInvert_Lambda([this]() -> bool
				{
					bool Value = false;
					if (InvertHandle.IsValid()) { InvertHandle->GetValue(Value); }
					return Value;
				})
			]
		];

	// 2. Add all child properties normally
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(i);
		if (Child.IsValid())
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}

	// 3. Register invalidation delegates
	auto InvalidatePreview = FSimpleDelegate::CreateLambda([WeakPreview = TWeakPtr<SPCGExEdgeEndpointsCompareNumPreview>(PreviewWidget)]()
	{
		if (TSharedPtr<SPCGExEdgeEndpointsCompareNumPreview> Pin = WeakPreview.Pin())
		{
			Pin->Invalidate(EInvalidateWidgetReason::Paint);
		}
	});

	auto RegisterInvalidation = [&InvalidatePreview](const TSharedPtr<IPropertyHandle>& Handle)
	{
		if (Handle.IsValid())
		{
			Handle->SetOnPropertyValueChanged(InvalidatePreview);
		}
	};

	RegisterInvalidation(ComparisonHandle);
	RegisterInvalidation(ToleranceHandle);
	RegisterInvalidation(InvertHandle);
}
