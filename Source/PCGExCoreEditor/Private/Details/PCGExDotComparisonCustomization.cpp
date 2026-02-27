// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Details/PCGExDotComparisonCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Details/Widgets/SPCGExDotComparisonPreview.h"
#include "Math/PCGExMath.h"
#include "Widgets/Layout/SBox.h"

TSharedRef<IPropertyTypeCustomization> FPCGExDotComparisonCustomization::MakeInstance()
{
	return MakeShareable(new FPCGExDotComparisonCustomization());
}

void FPCGExDotComparisonCustomization::CustomizeHeader(
	TSharedRef<IPropertyHandle> PropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Cache all child handles
	DomainHandle = PropertyHandle->GetChildHandle(TEXT("Domain"));
	ComparisonHandle = PropertyHandle->GetChildHandle(TEXT("Comparison"));
	UnsignedHandle = PropertyHandle->GetChildHandle(TEXT("bUnsignedComparison"));
	DotConstantHandle = PropertyHandle->GetChildHandle(TEXT("DotConstant"));
	DotToleranceHandle = PropertyHandle->GetChildHandle(TEXT("DotTolerance"));
	DegreesConstantHandle = PropertyHandle->GetChildHandle(TEXT("DegreesConstant"));
	DegreesToleranceHandle = PropertyHandle->GetChildHandle(TEXT("DegreesTolerance"));
	ThresholdInputHandle = PropertyHandle->GetChildHandle(TEXT("ThresholdInput"));

	bIsStaticVariant = !ThresholdInputHandle.IsValid();

	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FPCGExDotComparisonCustomization::CustomizeChildren(
	TSharedRef<IPropertyHandle> PropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// 1. Insert the arc visualization as the first custom row
	ChildBuilder.AddCustomRow(FText::FromString(TEXT("Preview")))
		.WholeRowContent()
		[
			SNew(SBox)
			.HeightOverride(140.0f)
			.HAlign(HAlign_Center)
			[
				SAssignNew(PreviewWidget, SPCGExDotComparisonPreview)
				.Comparison_Lambda([this]() -> EPCGExComparison
				{
					uint8 Value = 0;
					if (ComparisonHandle.IsValid()) { ComparisonHandle->GetValue(Value); }
					return static_cast<EPCGExComparison>(Value);
				})
				.bUnsigned_Lambda([this]() -> bool
				{
					bool Value = false;
					if (UnsignedHandle.IsValid()) { UnsignedHandle->GetValue(Value); }
					return Value;
				})
				.ComparisonThreshold_Lambda([this]() -> double
				{
					return GetComparisonThreshold();
				})
				.ComparisonTolerance_Lambda([this]() -> double
				{
					return GetComparisonTolerance();
				})
				.bShowThreshold_Lambda([this]() -> bool
				{
					return !IsAttributeMode();
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
	auto InvalidatePreview = FSimpleDelegate::CreateLambda([WeakPreview = TWeakPtr<SPCGExDotComparisonPreview>(PreviewWidget)]()
	{
		if (TSharedPtr<SPCGExDotComparisonPreview> Pin = WeakPreview.Pin())
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

	RegisterInvalidation(DomainHandle);
	RegisterInvalidation(ComparisonHandle);
	RegisterInvalidation(UnsignedHandle);
	RegisterInvalidation(DotConstantHandle);
	RegisterInvalidation(DotToleranceHandle);
	RegisterInvalidation(DegreesConstantHandle);
	RegisterInvalidation(DegreesToleranceHandle);
	RegisterInvalidation(ThresholdInputHandle);
}

double FPCGExDotComparisonCustomization::GetComparisonThreshold() const
{
	// Read domain
	uint8 DomainValue = 0;
	if (DomainHandle.IsValid()) { DomainHandle->GetValue(DomainValue); }
	const EPCGExAngularDomain Domain = static_cast<EPCGExAngularDomain>(DomainValue);

	// Read unsigned
	bool bIsUnsigned = false;
	if (UnsignedHandle.IsValid()) { UnsignedHandle->GetValue(bIsUnsigned); }

	// Compute raw threshold in dot product space
	double RawDot = 0.0;
	if (Domain == EPCGExAngularDomain::Degrees)
	{
		double Degrees = 90.0;
		if (DegreesConstantHandle.IsValid()) { DegreesConstantHandle->GetValue(Degrees); }
		RawDot = PCGExMath::DegreesToDot(180.0 - Degrees);
	}
	else
	{
		if (bIsStaticVariant)
		{
			double Dot = 0.5;
			if (DotConstantHandle.IsValid()) { DotConstantHandle->GetValue(Dot); }
			RawDot = Dot;
		}
		else
		{
			double Dot = 0.0;
			if (DotConstantHandle.IsValid()) { DotConstantHandle->GetValue(Dot); }
			RawDot = Dot;
		}
	}

	// Remap to comparison space (mirrors Init() logic)
	if (bIsUnsigned)
	{
		return FMath::Abs(RawDot);
	}

	return (1.0 + RawDot) * 0.5;
}

double FPCGExDotComparisonCustomization::GetComparisonTolerance() const
{
	// Read domain
	uint8 DomainValue = 0;
	if (DomainHandle.IsValid()) { DomainHandle->GetValue(DomainValue); }
	const EPCGExAngularDomain Domain = static_cast<EPCGExAngularDomain>(DomainValue);

	// Read unsigned
	bool bIsUnsigned = false;
	if (UnsignedHandle.IsValid()) { UnsignedHandle->GetValue(bIsUnsigned); }

	if (bIsStaticVariant)
	{
		// Mirror FPCGExStaticDotComparisonDetails::Init()
		double RawTol = 0.0;
		if (Domain == EPCGExAngularDomain::Degrees)
		{
			double DegTol = 0.1;
			if (DegreesToleranceHandle.IsValid()) { DegreesToleranceHandle->GetValue(DegTol); }
			RawTol = PCGExMath::DegreesToDot(180.0 - DegTol);
		}
		else
		{
			double DotTol = 0.1;
			if (DotToleranceHandle.IsValid()) { DotToleranceHandle->GetValue(DotTol); }
			RawTol = DotTol;
		}

		if (bIsUnsigned)
		{
			return FMath::Abs(RawTol);
		}

		return (1.0 + RawTol) * 0.5;
	}

	// Mirror FPCGExDotComparisonDetails::Init()
	// No unsigned-specific tolerance path in the dynamic variant
	if (Domain == EPCGExAngularDomain::Degrees)
	{
		double DegTol = 0.1;
		if (DegreesToleranceHandle.IsValid()) { DegreesToleranceHandle->GetValue(DegTol); }
		return (1.0 + PCGExMath::DegreesToDot(180.0 - DegTol)) * 0.5;
	}

	double DotTol = 0.1;
	if (DotToleranceHandle.IsValid()) { DotToleranceHandle->GetValue(DotTol); }
	return DotTol;
}

bool FPCGExDotComparisonCustomization::IsAttributeMode() const
{
	if (!ThresholdInputHandle.IsValid()) { return false; } // Static variant is always constant

	uint8 InputType = 0;
	ThresholdInputHandle->GetValue(InputType);
	return InputType != 0; // 0 = Constant, 1 = Attribute
}
