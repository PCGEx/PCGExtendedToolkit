// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * Struct customization for FPCGExCollectionEntryRef: the entry picker as one header row, no children.
 * Registered globally, so it also reaches struct-on-scope panels (the collection grid's detail pane), where
 * an entry's own customization never runs. Options resolve through the picker's provider registry.
 */
class PCGEXCOLLECTIONSEDITOR_API FPCGExCollectionEntryRefCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
	}
};
