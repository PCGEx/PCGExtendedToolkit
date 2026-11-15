// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "Details/PCGExAttributeSourceToTargetCustomization.h"

/** FPCGExAttributeToPinDetails on a single row: [Source] -> [Target] [Type]. */
class FPCGExAttributeToPinCustomization : public FPCGExAttributeSourceToTargetCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	virtual void AddInlineControls(const TSharedRef<IPropertyHandle>& PropertyHandle, const TSharedRef<SHorizontalBox>& Row) override;
};
