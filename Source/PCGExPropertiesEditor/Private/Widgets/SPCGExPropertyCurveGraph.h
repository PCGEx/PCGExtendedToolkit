// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#pragma once

#include "CoreMinimal.h"
#include "SPCGExPropertyCurveWidgetBase.h"

class FPCGExPropertyCurveEditController;

/**
 * Ramp graph: paints the evaluated curve and one draggable point per key. The X/Y axes auto-frame to
 * the controller's time/value frames (X always spans at least [0,1]); this widget only maps geometry.
 * The gesture grammar (select/drag/add/delete, Ctrl axis-lock, capture-loss handling) lives in the
 * shared SPCGExPropertyCurveWidgetBase; this class supplies painting, the 2D hit-test, and the 2D drag/add.
 */
class SPCGExPropertyCurveGraph : public SPCGExPropertyCurveWidgetBase
{
public:
	SLATE_BEGIN_ARGS(SPCGExPropertyCurveGraph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FPCGExPropertyCurveEditController>& InController);

	//~ Begin SWidget interface
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	//~ End SWidget interface

protected:
	//~ Begin SPCGExPropertyCurveWidgetBase interface
	virtual int32 HitTestKey(const FVector2D& Local, const FVector2D& Size) const override;
	virtual void ApplyDrag(const FVector2D& Local, const FVector2D& Size, bool bCtrlDown) override;
	virtual void AddKeyAtCursor(const FVector2D& Local, const FVector2D& Size) override;
	//~ End SPCGExPropertyCurveWidgetBase interface

private:
	float ValueToLocalY(float Value, const FVector2D& Size) const;
	float LocalYToValue(float LocalY, const FVector2D& Size) const;
	FVector2D KeyToLocal(int32 Index, const FVector2D& Size) const;

	static constexpr float HandleSize = 9.0f;
	static constexpr float HandleHitRadius = 11.0f;
	static constexpr float DesiredWidth = 320.0f;
	static constexpr float DesiredHeight = 110.0f;
	// Uniform grid density for the painted curve; key times are additionally injected as exact
	// sample points at paint time (see OnPaint), so steps/kinks stay crisp independently of this.
	static constexpr int32 CurveSamples = 256;
};
