// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/
// Adapted from PCGExRamps' inline ramp editor (PCGExRampsEditor/Private/Widgets) -- keep the two in sync when fixing gesture/transaction bugs.

#include "SPCGExPropertyCurveWidgetBase.h"

#include "PCGExPropertyCurveEditController.h"

void SPCGExPropertyCurveWidgetBase::Init(const TSharedRef<FPCGExPropertyCurveEditController>& InController)
{
	Controller = InController;
	Controller->OnChanged.AddSP(this, &SPCGExPropertyCurveWidgetBase::HandleControllerChanged);
	Controller->OnSelectionChanged.AddSP(this, &SPCGExPropertyCurveWidgetBase::HandleSelectionChanged);
}

void SPCGExPropertyCurveWidgetBase::HandleControllerChanged(bool bInteractive)
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SPCGExPropertyCurveWidgetBase::HandleSelectionChanged()
{
	Invalidate(EInvalidateWidgetReason::Paint);
}

#pragma region Coordinate mapping

float SPCGExPropertyCurveWidgetBase::TimeToLocalX(float Time, const FVector2D& Size) const
{
	const float Left = Padding;
	const float Right = Size.X - Padding;
	// Map through the time frame (no clamp): a key dragged past the frozen frame's edge tracks the cursor
	// beyond the plot and pulls back into view when the frame refits on release.
	const float Alpha = Controller->TimeToFrameAlpha(Time);
	return Left + Alpha * FMath::Max(Right - Left, 1.0f);
}

float SPCGExPropertyCurveWidgetBase::LocalXToTime(float LocalX, const FVector2D& Size) const
{
	const float Left = Padding;
	const float Right = Size.X - Padding;
	const float Alpha = (LocalX - Left) / FMath::Max(Right - Left, 1.0f);
	return Controller->FrameAlphaToTime(Alpha);
}

#pragma endregion

#pragma region Input

void SPCGExPropertyCurveWidgetBase::EndDrag()
{
	bDragging = false;
	DragHandle = FKeyHandle::Invalid();
	if (bDidDrag && Controller.IsValid())
	{
		// Finalizing (non-interactive) commit -- closes the property transaction the drag opened.
		Controller->CommitInteractive();
	}
	bDidDrag = false;
}

FReply SPCGExPropertyCurveWidgetBase::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Controller.IsValid())
	{
		return FReply::Unhandled();
	}

	// A drag holds mouse capture, so any other button press is routed here mid-gesture. Swallow it:
	// mutating keys (delete/add) mid-drag would prematurely close and re-open the interactive transaction,
	// splitting one gesture into several undo steps. The drag ends only via its left-up or capture loss.
	if (bDragging)
	{
		return FReply::Handled();
	}

	const FVector2D Size = MyGeometry.GetLocalSize();
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 HitIdx = HitTestKey(Local, Size);

	if (MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		if (HitIdx != INDEX_NONE)
		{
			Controller->DeleteKeyByIndex(HitIdx);
		}
		return FReply::Handled();
	}

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Alt + click a key: delete it (alternative to middle-click).
		if (MouseEvent.IsAltDown())
		{
			if (HitIdx != INDEX_NONE)
			{
				Controller->DeleteKeyByIndex(HitIdx);
			}
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}

		// Shift + click: add a key at the cursor.
		if (MouseEvent.IsShiftDown())
		{
			AddKeyAtCursor(Local, Size);
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}

		if (HitIdx != INDEX_NONE)
		{
			Controller->SetSelectedKeyByIndex(HitIdx);
			bDragging = true;
			bDidDrag = false;
			DragHandle = Controller->GetKeyHandleAt(HitIdx);
			return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}

		Controller->ClearSelection();
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Non-destructive: select only.
		if (HitIdx != INDEX_NONE)
		{
			Controller->SetSelectedKeyByIndex(HitIdx);
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SPCGExPropertyCurveWidgetBase::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && HasMouseCapture() && Controller.IsValid())
	{
		const FVector2D Size = MyGeometry.GetLocalSize();
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		bDidDrag = true;
		ApplyDrag(Local, Size, MouseEvent.IsControlDown());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SPCGExPropertyCurveWidgetBase::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bDragging)
	{
		EndDrag();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

void SPCGExPropertyCurveWidgetBase::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// Capture stolen mid-drag (window deactivation, Alt+Tab, a popup taking capture) never delivers a
	// button-up. End the gesture here so CommitInteractive() still runs and the transaction is closed;
	// otherwise it stays open -- undo breaks and PCG never settles out of interactive regeneration.
	if (bDragging)
	{
		EndDrag();
	}
}

FReply SPCGExPropertyCurveWidgetBase::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && Controller.IsValid())
	{
		const FVector2D Size = MyGeometry.GetLocalSize();
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		if (HitTestKey(Local, Size) == INDEX_NONE)
		{
			// Add on the curve at the clicked time (Shift+click is the place-at-cursor variant).
			Controller->AddKeyAtTime(LocalXToTime(Local.X, Size));
			return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}
	}
	return FReply::Unhandled();
}

FReply SPCGExPropertyCurveWidgetBase::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Controller.IsValid() && (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace))
	{
		Controller->DeleteSelectedKey();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#pragma endregion
