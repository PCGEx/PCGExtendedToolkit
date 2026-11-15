// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "IPropertyTypeCustomization.h"

class SHorizontalBox;

/**
 * Customizes FPCGExAttributeSourceToTargetDetails, and structs deriving from it, into a compact inline row:
 *   [Source] -> [Target]
 * The arrowed value box edits Target: committing text enables bOutputToDifferentName + sets Target,
 * clearing it disables the remap. While not remapping, the box shows a greyed "= {Source}" hint, since
 * GetOutputName() falls back to Source.
 */
class PCGEXCOREEDITOR_API FPCGExAttributeSourceToTargetCustomization : public IPropertyTypeCustomization
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

protected:
	/** Extra controls for a derived struct's own members, placed after the Target box. */
	virtual void AddInlineControls(const TSharedRef<IPropertyHandle>& PropertyHandle, const TSharedRef<SHorizontalBox>& Row)
	{
	}
};
