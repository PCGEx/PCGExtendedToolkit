// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExAdjacencySettingsCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Widgets/SPCGExAdjacencyPreview.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExAdjacencySettingsCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExAdjacencySettingsCustomization());
}

void FPCGExAdjacencySettingsCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Cache all child handles
	ModeHandle = PropertyHandle->GetChildHandle(TEXT("Mode"));
	ConsolidationHandle = PropertyHandle->GetChildHandle(TEXT("Consolidation"));
	ThresholdComparisonHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdComparison"));
	ThresholdTypeHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdType"));
	ThresholdInputHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdInput"));
	DiscreteThresholdHandle = PropertyHandle->GetChildHandle(TEXT("DiscreteThreshold"));
	RelativeThresholdHandle = PropertyHandle->GetChildHandle(TEXT("RelativeThreshold"));
	RoundingHandle = PropertyHandle->GetChildHandle(TEXT("Rounding"));
	ThresholdToleranceHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdTolerance"));

	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGExAdjacencySettingsCustomization::CustomizeChildren(
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
				SAssignNew(PreviewWidget, SPCGExAdjacencyPreview)
				.Mode_Lambda([this]() -> EPCGExAdjacencyTestMode
				{
					uint8 Value = 0;
					if (ModeHandle.IsValid()) { ModeHandle->GetValue(Value); }
					return static_cast<EPCGExAdjacencyTestMode>(Value);
				})
				.Consolidation_Lambda([this]() -> EPCGExAdjacencyGatherMode
				{
					uint8 Value = 0;
					if (ConsolidationHandle.IsValid()) { ConsolidationHandle->GetValue(Value); }
					return static_cast<EPCGExAdjacencyGatherMode>(Value);
				})
				.ThresholdComparison_Lambda([this]() -> EPCGExComparison
				{
					uint8 Value = 0;
					if (ThresholdComparisonHandle.IsValid()) { ThresholdComparisonHandle->GetValue(Value); }
					return static_cast<EPCGExComparison>(Value);
				})
				.ThresholdType_Lambda([this]() -> EPCGExMeanMeasure
				{
					uint8 Value = 0;
					if (ThresholdTypeHandle.IsValid()) { ThresholdTypeHandle->GetValue(Value); }
					return static_cast<EPCGExMeanMeasure>(Value);
				})
				.DiscreteThreshold_Lambda([this]() -> int32
				{
					int32 Value = 1;
					if (DiscreteThresholdHandle.IsValid()) { DiscreteThresholdHandle->GetValue(Value); }
					return Value;
				})
				.RelativeThreshold_Lambda([this]() -> double
				{
					double Value = 0.5;
					if (RelativeThresholdHandle.IsValid()) { RelativeThresholdHandle->GetValue(Value); }
					return Value;
				})
				.Rounding_Lambda([this]() -> EPCGExRelativeThresholdRoundingMode
				{
					uint8 Value = 0;
					if (RoundingHandle.IsValid()) { RoundingHandle->GetValue(Value); }
					return static_cast<EPCGExRelativeThresholdRoundingMode>(Value);
				})
				.ThresholdTolerance_Lambda([this]() -> int32
				{
					int32 Value = 0;
					if (ThresholdToleranceHandle.IsValid()) { ThresholdToleranceHandle->GetValue(Value); }
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

	// 2. Add all child properties normally (EditCondition metadata handles visibility)
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

	// 3. Register invalidation delegates for live preview updates
	auto InvalidatePreview = FSimpleDelegate::CreateLambda([WeakPreview = TWeakPtr<SPCGExAdjacencyPreview>(PreviewWidget)]()
	{
		if (TSharedPtr<SPCGExAdjacencyPreview> Pin = WeakPreview.Pin())
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
	RegisterInvalidation(ConsolidationHandle);
	RegisterInvalidation(ThresholdComparisonHandle);
	RegisterInvalidation(ThresholdTypeHandle);
	RegisterInvalidation(ThresholdInputHandle);
	RegisterInvalidation(DiscreteThresholdHandle);
	RegisterInvalidation(RelativeThresholdHandle);
	RegisterInvalidation(RoundingHandle);
	RegisterInvalidation(ThresholdToleranceHandle);
}
