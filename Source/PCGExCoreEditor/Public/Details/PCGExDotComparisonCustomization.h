// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

class SPCGExDotComparisonPreview;

/**
 * Shared IPropertyTypeCustomization for FPCGExStaticDotComparisonDetails and FPCGExDotComparisonDetails.
 * Embeds an arc visualization above the standard property rows.
 * Detects which variant it handles by probing for the ThresholdInput child handle.
 */
class FPCGExDotComparisonCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		class IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	TSharedPtr<IPropertyHandle> DomainHandle;
	TSharedPtr<IPropertyHandle> ComparisonHandle;
	TSharedPtr<IPropertyHandle> UnsignedHandle;
	TSharedPtr<IPropertyHandle> DotConstantHandle;
	TSharedPtr<IPropertyHandle> DotToleranceHandle;
	TSharedPtr<IPropertyHandle> DegreesConstantHandle;
	TSharedPtr<IPropertyHandle> DegreesToleranceHandle;
	TSharedPtr<IPropertyHandle> ThresholdInputHandle; // Only valid for FPCGExDotComparisonDetails

	bool bIsStaticVariant = true;

	TSharedPtr<SPCGExDotComparisonPreview> PreviewWidget;

	/** Compute comparison threshold in internal comparison space, mirroring the struct's Init() logic. */
	double GetComparisonThreshold() const;

	/** Compute comparison tolerance in internal comparison space, mirroring the struct's Init() logic. */
	double GetComparisonTolerance() const;

	/** Whether the dynamic variant is in attribute mode (no constant threshold to visualize). */
	bool IsAttributeMode() const;
};
