// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

class SPCGExEdgeEndpointsCheckPreview;

/**
 * IPropertyTypeCustomization for FPCGExEdgeEndpointsCheckFilterConfig.
 * Embeds a 4-panel truth table visualization above the standard property rows.
 */
class FPCGExEdgeEndpointsCheckFilterConfigCustomization : public IPropertyTypeCustomization
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
	TSharedPtr<IPropertyHandle> ModeHandle;
	TSharedPtr<IPropertyHandle> ExpectsHandle;
	TSharedPtr<IPropertyHandle> InvertHandle;

	TSharedPtr<SPCGExEdgeEndpointsCheckPreview> PreviewWidget;
};
