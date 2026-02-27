// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

class SPCGExAdjacencyPreview;

/**
 * IPropertyTypeCustomization for FPCGExAdjacencySettings.
 * Embeds a 3-panel star-diagram visualization above the standard property rows,
 * showing how different adjacency counts evaluate against the current settings.
 */
class FPCGExAdjacencySettingsCustomization : public IPropertyTypeCustomization
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
	TSharedPtr<IPropertyHandle> ConsolidationHandle;
	TSharedPtr<IPropertyHandle> ThresholdComparisonHandle;
	TSharedPtr<IPropertyHandle> ThresholdTypeHandle;
	TSharedPtr<IPropertyHandle> ThresholdInputHandle;
	TSharedPtr<IPropertyHandle> DiscreteThresholdHandle;
	TSharedPtr<IPropertyHandle> RelativeThresholdHandle;
	TSharedPtr<IPropertyHandle> RoundingHandle;
	TSharedPtr<IPropertyHandle> ThresholdToleranceHandle;

	TSharedPtr<SPCGExAdjacencyPreview> PreviewWidget;
};
