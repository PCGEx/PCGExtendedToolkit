// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

class SPCGExEdgeNeighborsCountPreview;

/**
 * IPropertyTypeCustomization for FPCGExEdgeNeighborsCountFilterConfig.
 * Embeds a 3-panel neighbor count visualization above the standard property rows.
 */
class FPCGExEdgeNeighborsCountFilterConfigCustomization : public IPropertyTypeCustomization
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
	TSharedPtr<IPropertyHandle> ThresholdInputHandle;
	TSharedPtr<IPropertyHandle> ThresholdConstantHandle;
	TSharedPtr<IPropertyHandle> ModeHandle;
	TSharedPtr<IPropertyHandle> ComparisonHandle;
	TSharedPtr<IPropertyHandle> ToleranceHandle;
	TSharedPtr<IPropertyHandle> InvertHandle;

	TSharedPtr<SPCGExEdgeNeighborsCountPreview> PreviewWidget;
};
