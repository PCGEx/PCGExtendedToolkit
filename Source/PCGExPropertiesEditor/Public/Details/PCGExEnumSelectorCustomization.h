// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * Default IPropertyTypeCustomization for FPCGExEnumSelector.
 *
 * Renders a single-row inline editor (class picker + value picker) via
 * PCGExEnumSelectorWidget::Make. This is the customization that gets invoked anywhere
 * an FPCGExEnumSelector appears as a UPROPERTY in user-defined structs / classes.
 *
 * Registered against FPCGExEnumSelector::StaticStruct() in PCGExPropertiesEditor module
 * startup. Replaces the engine's FEnumSelectorDetails.
 */
class PCGEXPROPERTIESEDITOR_API FPCGExEnumSelectorCustomization : public IPropertyTypeCustomization
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
};
