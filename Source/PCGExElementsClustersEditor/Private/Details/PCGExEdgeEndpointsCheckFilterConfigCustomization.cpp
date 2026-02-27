// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExEdgeEndpointsCheckFilterConfigCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Widgets/SPCGExEdgeEndpointsCheckPreview.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExEdgeEndpointsCheckFilterConfigCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExEdgeEndpointsCheckFilterConfigCustomization());
}

void FPCGExEdgeEndpointsCheckFilterConfigCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ModeHandle = PropertyHandle->GetChildHandle(TEXT("Mode"));
	ExpectsHandle = PropertyHandle->GetChildHandle(TEXT("Expects"));
	InvertHandle = PropertyHandle->GetChildHandle(TEXT("bInvert"));

	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGExEdgeEndpointsCheckFilterConfigCustomization::CustomizeChildren(
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
				SAssignNew(PreviewWidget, SPCGExEdgeEndpointsCheckPreview)
				.Mode_Lambda([this]() -> EPCGExEdgeEndpointsCheckMode
				{
					uint8 Value = 0;
					if (ModeHandle.IsValid()) { ModeHandle->GetValue(Value); }
					return static_cast<EPCGExEdgeEndpointsCheckMode>(Value);
				})
				.Expects_Lambda([this]() -> EPCGExFilterResult
				{
					uint8 Value = 0;
					if (ExpectsHandle.IsValid()) { ExpectsHandle->GetValue(Value); }
					return static_cast<EPCGExFilterResult>(Value);
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
	auto InvalidatePreview = FSimpleDelegate::CreateLambda([WeakPreview = TWeakPtr<SPCGExEdgeEndpointsCheckPreview>(PreviewWidget)]()
	{
		if (TSharedPtr<SPCGExEdgeEndpointsCheckPreview> Pin = WeakPreview.Pin())
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

	RegisterInvalidation(ModeHandle);
	RegisterInvalidation(ExpectsHandle);
	RegisterInvalidation(InvertHandle);
}
