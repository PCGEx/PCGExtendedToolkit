// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#pragma once

#include "CoreMinimal.h"
#include "Curves/RichCurve.h"
#include "Widgets/SCompoundWidget.h"

class FPCGExPropertyCurveEditController;

/**
 * Inline Houdini-style ramp editor: a normalized curve graph, a draggable key strip, and a numeric
 * inspector (Position / Value / Interpolation) for the selected key. All three views share one
 * FPCGExPropertyCurveEditController, which owns the working curve and emits OnChanged after every edit.
 */
class SPCGExPropertyCurveEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPCGExPropertyCurveEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FPCGExPropertyCurveEditController>& InController);

private:
	bool HasSelection() const;

	TOptional<float> GetSelectedTime() const;
	TOptional<float> GetSelectedValue() const;
	void SetSelectedTime(float NewTime, bool bInteractive);
	void SetSelectedValue(float NewValue, bool bInteractive);

	FText GetSelectedInterpText() const;
	TSharedRef<SWidget> BuildInterpMenu();
	void SetSelectedInterp(ERichCurveInterpMode Mode);

	TSharedRef<SWidget> BuildInspector();

	TSharedPtr<FPCGExPropertyCurveEditController> Controller;

	static constexpr float GraphHeight = 110.0f;
	static constexpr float StripHeight = 18.0f;
};
