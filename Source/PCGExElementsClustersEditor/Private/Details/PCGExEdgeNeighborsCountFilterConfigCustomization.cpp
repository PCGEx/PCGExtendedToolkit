// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExEdgeNeighborsCountFilterConfigCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Widgets/SPCGExEdgeNeighborsCountPreview.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExEdgeNeighborsCountFilterConfigCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExEdgeNeighborsCountFilterConfigCustomization());
}

void FPCGExEdgeNeighborsCountFilterConfigCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	ThresholdInputHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdInput"));
	ThresholdConstantHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdConstant"));
	ModeHandle = PropertyHandle->GetChildHandle(TEXT("Mode"));
	ComparisonHandle = PropertyHandle->GetChildHandle(TEXT("Comparison"));
	ToleranceHandle = PropertyHandle->GetChildHandle(TEXT("Tolerance"));
	InvertHandle = PropertyHandle->GetChildHandle(TEXT("bInvert"));

	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGExEdgeNeighborsCountFilterConfigCustomization::CustomizeChildren(
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
				SAssignNew(PreviewWidget, SPCGExEdgeNeighborsCountPreview)
				.Mode_Lambda([this]() -> EPCGExRefineEdgeThresholdMode
				{
					uint8 Value = 0;
					if (ModeHandle.IsValid()) { ModeHandle->GetValue(Value); }
					return static_cast<EPCGExRefineEdgeThresholdMode>(Value);
				})
				.Comparison_Lambda([this]() -> EPCGExComparison
				{
					uint8 Value = 0;
					if (ComparisonHandle.IsValid()) { ComparisonHandle->GetValue(Value); }
					return static_cast<EPCGExComparison>(Value);
				})
				.ThresholdConstant_Lambda([this]() -> int32
				{
					int32 Value = 2;
					if (ThresholdConstantHandle.IsValid()) { ThresholdConstantHandle->GetValue(Value); }
					return Value;
				})
				.Tolerance_Lambda([this]() -> int32
				{
					int32 Value = 0;
					if (ToleranceHandle.IsValid()) { ToleranceHandle->GetValue(Value); }
					return Value;
				})
				.bInvert_Lambda([this]() -> bool
				{
					bool Value = false;
					if (InvertHandle.IsValid()) { InvertHandle->GetValue(Value); }
					return Value;
				})
				.bShowThreshold_Lambda([this]() -> bool
				{
					if (!ThresholdInputHandle.IsValid()) { return true; }
					uint8 InputType = 0;
					ThresholdInputHandle->GetValue(InputType);
					return InputType == 0; // 0 = Constant
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
	auto InvalidatePreview = FSimpleDelegate::CreateLambda([WeakPreview = TWeakPtr<SPCGExEdgeNeighborsCountPreview>(PreviewWidget)]()
	{
		if (TSharedPtr<SPCGExEdgeNeighborsCountPreview> Pin = WeakPreview.Pin())
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

	RegisterInvalidation(ThresholdInputHandle);
	RegisterInvalidation(ThresholdConstantHandle);
	RegisterInvalidation(ModeHandle);
	RegisterInvalidation(ComparisonHandle);
	RegisterInvalidation(ToleranceHandle);
	RegisterInvalidation(InvertHandle);
}
