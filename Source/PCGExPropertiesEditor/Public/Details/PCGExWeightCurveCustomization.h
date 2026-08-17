// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IPropertyHandle;

/**
 * Detail customization for FPCGExWeightCurve (PCGExCore): any host UPROPERTY of this type renders
 * through the inline PCGEx curve editor as a full-width child row (FPCGExProperty_FloatCurve's layout).
 *
 * Optional clamp metas on the host UPROPERTY constrain editing:
 *   meta = (PCGExCurveTimeMin="0", PCGExCurveTimeMax="1", PCGExCurveValueMin="0", PCGExCurveValueMax="10")
 */
class FPCGExWeightCurveCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	//~ Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IPropertyTypeCustomization interface

private:
	/** Handle to the FRuntimeFloatCurve 'Curve' member of FPCGExWeightCurve. */
	TSharedPtr<IPropertyHandle> CurveHandle;
};
