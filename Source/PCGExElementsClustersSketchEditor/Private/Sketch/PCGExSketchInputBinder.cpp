// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sketch/PCGExSketchInputBinder.h"

#include "InputRouter.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "Sketch/PCGExSketchEditController.h"

namespace PCGExSketchInputBinder
{
	constexpr int CtrlModifierID = 1;
	constexpr int ShiftModifierID = 2;
	constexpr int AltModifierID = 3;

	// Depth reported for hits with no meaningful ray depth (empty-space add capture, blanket hover):
	// far enough that anything with a real depth wins the router's arbitration.
	constexpr double FarHitDepth = 1.0e8;
}

void UPCGExSketchInputBinder::Initialize(FResolveControllerFn InResolve, FForEachControllerFn InForEach)
{
	ResolveFn = MoveTemp(InResolve);
	ForEachFn = MoveTemp(InForEach);

	BehaviorSet = NewObject<UInputBehaviorSet>(this);

	USingleClickOrDragInputBehavior* ClickOrDrag = NewObject<USingleClickOrDragInputBehavior>(this);
	ClickOrDrag->Initialize(this, this);
	// CRITICAL: the default (true) sends a press that missed the CLICK test into the DRAG test at the
	// press ray -- depth-ambiguous, so casual empty-space drags silently grabbed whatever vertex sat
	// within screen radius at ANY depth. A drag may only ever grow out of a captured click.
	ClickOrDrag->bBeginDragIfClickTargetNotHit = false;
	ClickOrDrag->Modifiers.RegisterModifier(PCGExSketchInputBinder::CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	ClickOrDrag->Modifiers.RegisterModifier(PCGExSketchInputBinder::ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	ClickOrDrag->Modifiers.RegisterModifier(PCGExSketchInputBinder::AltModifierID, FInputDeviceState::IsAltKeyDown);
	BehaviorSet->Add(ClickOrDrag);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	HoverBehavior->Modifiers.RegisterModifier(PCGExSketchInputBinder::CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	HoverBehavior->Modifiers.RegisterModifier(PCGExSketchInputBinder::ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	HoverBehavior->Modifiers.RegisterModifier(PCGExSketchInputBinder::AltModifierID, FInputDeviceState::IsAltKeyDown);
	BehaviorSet->Add(HoverBehavior);
}

void UPCGExSketchInputBinder::RegisterWith(UInputRouter* InRouter)
{
	if (InRouter)
	{
		InRouter->RegisterSource(this);
	}
}

void UPCGExSketchInputBinder::DeregisterFrom(UInputRouter* InRouter)
{
	if (InRouter)
	{
		InRouter->DeregisterSource(this);
	}
}

TSharedPtr<FPCGExSketchEditController> UPCGExSketchInputBinder::Resolve(const FRay& WorldRay) const
{
	return ResolveFn ? ResolveFn(WorldRay) : nullptr;
}

void UPCGExSketchInputBinder::ForEach(TFunctionRef<void(FPCGExSketchEditController&)> InFn) const
{
	if (ForEachFn)
	{
		ForEachFn(InFn);
	}
}

bool UPCGExSketchInputBinder::HandleKeyDown(const FKey& InKey)
{
	if (InKey == EKeys::Tab)
	{
		// Consumed ONLY while a placement gesture is live, so Tab keeps its editor-wide meaning
		// everywhere else.
		bool bCycled = false;
		ForEach([&bCycled](FPCGExSketchEditController& Controller)
		{
			bCycled |= Controller.CyclePlacementGuide();
		});
		return bCycled;
	}

	if (InKey == EKeys::Delete)
	{
		bool bDeleted = false;
		ForEach([&bDeleted](FPCGExSketchEditController& Controller)
		{
			if (Controller.HasSelection())
			{
				Controller.DeleteSelection();
				bDeleted = true;
			}
		});
		return bDeleted;
	}

	if (InKey == EKeys::Escape)
	{
		// Least destructive first: a held guide is released before the gesture holding it is abandoned.
		bool bReleased = false;
		ForEach([&bReleased](FPCGExSketchEditController& Controller)
		{
			bReleased |= Controller.ReleasePlacementGuide();
		});
		if (bReleased)
		{
			return true;
		}

		bool bHandled = false;
		ForEach([&bHandled](FPCGExSketchEditController& Controller)
		{
			if (Controller.GetDragMode() != FPCGExSketchEditController::EDragMode::None)
			{
				Controller.CancelDrag();
				bHandled = true;
			}
			else if (Controller.HasSelection())
			{
				Controller.ClearSelection();
				Controller.NotifyModelChanged();
				bHandled = true;
			}
		});
		return bHandled;
	}

	return false;
}

bool UPCGExSketchInputBinder::HandleAltClick(const FRay& WorldRay)
{
	const TSharedPtr<FPCGExSketchEditController> Controller = Resolve(WorldRay);
	return Controller ? Controller->DeleteAtRay(WorldRay) : false;
}

FInputRayHit UPCGExSketchInputBinder::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	const TSharedPtr<FPCGExSketchEditController> Controller = Resolve(ClickPos.WorldRay);
	const FPCGExSketchHit Hit = Controller ? Controller->HitTest(ClickPos.WorldRay) : FPCGExSketchHit();

	// Alt is the camera-orbit modifier: never claim it here. The delete gesture rides each host's
	// legacy click path instead (HandleAltClick), because Alt+LMB never reaches this router at all.
	if (bAltDown)
	{
		return FInputRayHit();
	}

	if (Hit.IsHit())
	{
		return FInputRayHit(Hit.RayT);
	}
	// Ctrl+click on empty space adds a vertex, so that click must be captured; a plain empty click
	// falls through to the camera (deselect lives on Escape -- capturing it would eat LMB camera drags).
	if (bCtrlDown && Controller)
	{
		return FInputRayHit(PCGExSketchInputBinder::FarHitDepth);
	}
	return FInputRayHit();
}

void UPCGExSketchInputBinder::OnClicked(const FInputDeviceRay& ClickPos)
{
	if (const TSharedPtr<FPCGExSketchEditController> Controller = Resolve(ClickPos.WorldRay))
	{
		Controller->HandleClick(ClickPos.WorldRay, /*bAdditive*/ bCtrlDown, /*bAddOnEmpty*/ bCtrlDown);
	}
}

FInputRayHit UPCGExSketchInputBinder::CanBeginClickDragSequence(const FInputDeviceRay& PressPos)
{
	// Ctrl states add/toggle intent -- a wiggled Ctrl+click must stay a CLICK, never convert into a
	// drag of some vertex that happened to sit near the cursor. Alt belongs to the camera.
	if (bCtrlDown || bAltDown)
	{
		return FInputRayHit();
	}

	const TSharedPtr<FPCGExSketchEditController> Controller = Resolve(PressPos.WorldRay);
	const FPCGExSketchHit Hit = Controller ? Controller->HitTest(PressPos.WorldRay) : FPCGExSketchHit();
	return Hit.IsVertex() ? FInputRayHit(Hit.RayT) : FInputRayHit();
}

void UPCGExSketchInputBinder::OnClickPress(const FInputDeviceRay& PressPos)
{
	// Latch for the whole drag: the controller that begins it must end it, even if the cursor wanders
	// over another sketch on the way.
	DragController = Resolve(PressPos.WorldRay);
	if (DragController)
	{
		DragController->BeginDrag(PressPos.WorldRay, /*bConnect*/ bShiftDown);
	}
}

void UPCGExSketchInputBinder::OnClickDrag(const FInputDeviceRay& DragPos)
{
	if (DragController)
	{
		DragController->UpdateDrag(DragPos.WorldRay);
	}
}

void UPCGExSketchInputBinder::OnClickRelease(const FInputDeviceRay& ReleasePos)
{
	if (DragController)
	{
		DragController->EndDrag(ReleasePos.WorldRay);
		DragController.Reset();
	}
}

void UPCGExSketchInputBinder::OnTerminateDragSequence()
{
	if (DragController)
	{
		DragController->CancelDrag();
		DragController.Reset();
	}
}

FInputRayHit UPCGExSketchInputBinder::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	// Hover everywhere a sketch exists: the controller decides what (if anything) the ray is over.
	return Resolve(PressPos.WorldRay) ? FInputRayHit(PCGExSketchInputBinder::FarHitDepth) : FInputRayHit();
}

void UPCGExSketchInputBinder::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	OnUpdateHover(DevicePos);
}

bool UPCGExSketchInputBinder::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	// Only the addressed controller hovers; every other one clears, so two sketches can never show a
	// hover highlight at once.
	const TSharedPtr<FPCGExSketchEditController> Addressed = Resolve(DevicePos.WorldRay);
	ForEach([&](FPCGExSketchEditController& Controller)
	{
		if (Addressed.Get() == &Controller)
		{
			Controller.UpdateHover(DevicePos.WorldRay);
		}
		else
		{
			Controller.ClearHover();
		}
	});
	return true;
}

void UPCGExSketchInputBinder::OnEndHover()
{
	ForEach([](FPCGExSketchEditController& Controller)
	{
		Controller.ClearHover();
	});
}

void UPCGExSketchInputBinder::OnUpdateModifierState(const int ModifierID, const bool bIsOn)
{
	// The ITF re-reports every registered modifier on EVERY capture update, not just on change, so
	// anything with a cost behind it has to diff first.
	if (ModifierID == PCGExSketchInputBinder::CtrlModifierID)
	{
		if (bCtrlDown == bIsOn)
		{
			return;
		}
		bCtrlDown = bIsOn;
		// Ctrl on empty space ADDS: preview where, guide included, before the click commits it.
		ForEach([bIsOn](FPCGExSketchEditController& Controller)
		{
			Controller.SetAddIntent(bIsOn);
		});
	}
	else if (ModifierID == PCGExSketchInputBinder::ShiftModifierID)
	{
		bShiftDown = bIsOn;
	}
	else if (ModifierID == PCGExSketchInputBinder::AltModifierID)
	{
		if (bAltDown == bIsOn)
		{
			return;
		}
		bAltDown = bIsOn;
		// The hovered element renders as a delete target while the modifier is held.
		ForEach([bIsOn](FPCGExSketchEditController& Controller)
		{
			Controller.SetDeleteIntent(bIsOn);
		});
	}
}
