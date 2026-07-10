// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#pragma once

#include "CoreMinimal.h"
#include "Curves/KeyHandle.h"
#include "Widgets/SLeafWidget.h"

class FPCGExPropertyCurveEditController;

/**
 * Shared base for the ramp editor's two draggable leaf widgets (graph + key strip). Carries the common
 * gesture grammar and X-axis mapping so it lives in one place:
 *
 *   - left-click a key     : select + begin a drag (tracked by FKeyHandle, so a reorder can't lose it)
 *   - drag                 : ApplyDrag() each move (subclass maps cursor -> key edit)
 *   - Shift + click        : AddKeyAtCursor() (subclass decides the value)
 *   - double-click empty   : add a key on the curve at that time
 *   - Alt / middle click   : delete the key under the cursor (down to a one-key minimum)
 *   - right-click          : select only; Del / Backspace: delete the selection
 *
 * The gesture ends on the left button-up OR on capture loss (OnMouseCaptureLost) -- both route through
 * EndDrag(), which calls CommitInteractive() to close the interactive property transaction. Losing that
 * finalizing call strands the transaction (broken undo, PCG stuck regenerating), so both exits matter.
 * While a drag is active, any other button press is swallowed so it can't mutate keys mid-gesture.
 *
 * Subclasses supply painting (OnPaint/ComputeDesiredSize) and the three hooks below.
 */
class SPCGExPropertyCurveWidgetBase : public SLeafWidget
{
public:
	//~ Begin SWidget interface
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	//~ End SWidget interface

protected:
	/** Stores the controller and subscribes to its change/selection delegates. Call from Construct. */
	void Init(const TSharedRef<FPCGExPropertyCurveEditController>& InController);

	void HandleControllerChanged(bool bInteractive);
	void HandleSelectionChanged();

	/** Shared X mapping through the controller's time frame (unclamped). */
	float TimeToLocalX(float Time, const FVector2D& Size) const;
	float LocalXToTime(float LocalX, const FVector2D& Size) const;

	/** End the active drag and finalize it (closes the interactive transaction if the key actually moved). */
	void EndDrag();

	// --- Subclass hooks (the parts that differ between graph and strip) ---

	/** Index of the key hit at Local, or INDEX_NONE (graph tests 2D, strip tests X only). */
	virtual int32 HitTestKey(const FVector2D& Local, const FVector2D& Size) const = 0;
	/** Apply an interactive drag move for DragHandle from the cursor (bCtrlDown = axis-lock request). */
	virtual void ApplyDrag(const FVector2D& Local, const FVector2D& Size, bool bCtrlDown) = 0;
	/** Add a key at the cursor on Shift+click (graph uses cursor value; strip samples the curve). */
	virtual void AddKeyAtCursor(const FVector2D& Local, const FVector2D& Size) = 0;

	TSharedPtr<FPCGExPropertyCurveEditController> Controller;

	bool bDragging = false;
	bool bDidDrag = false;
	/** Key being dragged, tracked by handle so a reorder mid-drag can't point us at the wrong key. */
	FKeyHandle DragHandle = FKeyHandle::Invalid();

	static constexpr float Padding = 8.0f;
};
