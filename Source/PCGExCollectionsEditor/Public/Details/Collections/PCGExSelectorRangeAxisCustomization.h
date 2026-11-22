// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

/**
 * Compact layout for FPCGExSelectorRangeAxis.
 *
 * Header row carries everything that's not the shorthand so array-element previews are useful
 * without expanding:
 *   NameContent  : [property name] + SourceMode radio
 *   ValueContent : Min/Max (or Range) property names + BoundaryMode cycle button
 *
 * Expanding the element reveals the single child row -- ValueSource -- which retains its
 * registered FPCGExInputShorthandSelectorDouble customization.
 */
class FPCGExSelectorRangeAxisCustomization : public IPropertyTypeCustomization
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
