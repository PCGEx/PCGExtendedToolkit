// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "InputBehaviorSet.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "UObject/Object.h"

#include "PCGExSketchInputBinder.generated.h"

class FPCGExSketchEditController;
class UInputRouter;

/**
 * THE sketch input layer, shared by every authoring host: the standalone Cluster Sketch editor and the
 * in-level editor mode both own one of these, so the gesture vocabulary exists in exactly one place --
 * a fix or an improvement here reaches both hosts for free.
 *
 * Hosts supply only policy: which controller a ray belongs to (one fixed controller in the asset
 * editor; the nearest among the selected components in the level mode), and how to reach every live
 * controller (for modifier intent and select-all/delete). Everything else -- behaviour construction,
 * the Ctrl/Shift/Alt map, modifier tracking, drag latching, key handling -- lives here.
 *
 * Input map:
 *  - click            select (Ctrl toggles; Ctrl on empty space ADDS a vertex)
 *  - drag from vertex move it (bound vertices stay snapped)
 *  - Shift+drag       connect; releasing on empty space extrudes a new vertex + edge
 *  - Alt+click        delete the vertex or edge under the cursor (LEGACY click path -- see HandleAltClick)
 *  - Delete / Escape  delete selection / cancel drag then clear selection
 * Plain clicks and drags on empty space are deliberately NOT captured, so camera navigation stays stock.
 */
UCLASS()
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API UPCGExSketchInputBinder
	: public UObject,
	  public IInputBehaviorSource,
	  public IClickBehaviorTarget,
	  public IClickDragBehaviorTarget,
	  public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	/** Controller a ray addresses, or null when the ray belongs to no sketch. */
	using FResolveControllerFn = TFunction<TSharedPtr<FPCGExSketchEditController>(const FRay& WorldRay)>;

	/** Every live controller, for host-wide operations (modifier intent, delete, escape). */
	using FForEachControllerFn = TFunction<void(TFunctionRef<void(FPCGExSketchEditController&)>)>;

	/** Build the behaviour set and bind host policy. Call once, before RegisterWith. */
	void Initialize(FResolveControllerFn InResolve, FForEachControllerFn InForEach);

	void RegisterWith(UInputRouter* InRouter);
	void DeregisterFrom(UInputRouter* InRouter);

	/** Delete / Escape. @return true when consumed. */
	bool HandleKeyDown(const FKey& InKey);

	/**
	 * Alt+click delete. Alt+LMB never reaches the ITF router (Alt starts camera tracking, which
	 * suppresses tools-context routing), so BOTH hosts must call this from their own legacy click path
	 * -- FEditorViewportClient::ProcessClick in the asset editor, UEdMode::HandleClick in the level mode.
	 * @return true when something was deleted.
	 */
	bool HandleAltClick(const FRay& WorldRay);

	bool IsAltDown() const { return bAltDown; }

	//~ IInputBehaviorSource
	virtual const UInputBehaviorSet* GetInputBehaviors() const override { return BehaviorSet; }

	//~ IClickBehaviorTarget
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	//~ IClickDragBehaviorTarget
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

	//~ IHoverBehaviorTarget
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	//~ IModifierToggleBehaviorTarget (one override serves every inherited copy)
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

private:
	TSharedPtr<FPCGExSketchEditController> Resolve(const FRay& WorldRay) const;
	void ForEach(TFunctionRef<void(FPCGExSketchEditController&)> InFn) const;

	UPROPERTY()
	TObjectPtr<UInputBehaviorSet> BehaviorSet;

	FResolveControllerFn ResolveFn;
	FForEachControllerFn ForEachFn;

	/** Latched for the whole drag: the controller that began it must be the one that ends it, even if
	 *  the cursor wanders over another sketch mid-drag. */
	TSharedPtr<FPCGExSketchEditController> DragController;

	bool bCtrlDown = false;
	bool bShiftDown = false;
	bool bAltDown = false;
};
