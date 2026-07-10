// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "Widgets/SCompoundWidget.h"

class IPropertyHandle;
class FPCGExPropertyCurveEditController;

/**
 * Self-contained inline curve editor bound to an FRuntimeFloatCurve property handle
 * (FPCGExProperty_FloatCurve::Value). Owns the transient working FRichCurve, the edit
 * controller, and the write-back:
 *
 *   pull:  Value.EditorCurveData -> working curve (linear 0->1 fallback on unusable payloads)
 *   push:  working curve -> every selected instance's Value.EditorCurveData, through
 *          NotifyPreChange / NotifyPostChange(Interactive|ValueSet) / NotifyFinishedChangingProperties
 *          so undo and PCG regeneration behave. The finalizing non-interactive push always
 *          writes (even on an unchanged value) to close the change the interactive one opened.
 *
 * Used by BOTH render paths of the Float Curve property:
 *   - FPCGExPropertyFloatCurveCustomization (schema / instanced-struct details)
 *   - the Compact inline-widget registry factory (override entry rows)
 */
class SPCGExPropertyCurveValueWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExPropertyCurveValueWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IPropertyHandle>& InValueHandle);

private:
	void PullFromProperty();
	void PushToProperty(bool bInteractive);
	void HandleCurveChanged(bool bInteractive);

	TSharedPtr<IPropertyHandle> ValueHandle;
	TSharedPtr<FRichCurve> CurveData = MakeShared<FRichCurve>();
	TSharedPtr<FPCGExPropertyCurveEditController> Controller;
};
