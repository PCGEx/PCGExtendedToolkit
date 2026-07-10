// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#pragma once

#include "CoreMinimal.h"
#include "SPCGExPropertyCurveWidgetBase.h"

class FPCGExPropertyCurveEditController;

/**
 * Houdini-style key strip: a horizontal bar of draggable gems, one per key, positioned by their time
 * within the shared X frame. Shares the graph's horizontal padding so a gem sits directly under its
 * point. The gesture grammar lives in the shared SPCGExPropertyCurveWidgetBase; this class supplies painting,
 * the X-only hit-test, and an X-only drag/add (value is edited on the graph or in the inspector).
 */
class SPCGExPropertyCurveKeyStrip : public SPCGExPropertyCurveWidgetBase
{
public:
	SLATE_BEGIN_ARGS(SPCGExPropertyCurveKeyStrip) {}
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
	static constexpr float GemWidth = 10.0f;
	static constexpr float GemHitHalfWidth = 7.0f;
	static constexpr float DesiredWidth = 320.0f;
	static constexpr float DesiredHeight = 18.0f;
};
