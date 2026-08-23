// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExVersion.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"

#include "PCGExSketchEditorMode.generated.h"

class FPCGExSketchComponentEditTarget;
class FPCGExSketchEditController;
class IToolsContextRenderAPI;
class UPCGExClusterSketchComponent;
class UPCGExSketchInputBinder;

struct FPCGExSketchPanelEntry;

/** One editable sketch: the component, its edit target, and the controller driving it. */
struct FPCGExSketchModeBinding
{
	TWeakObjectPtr<UPCGExClusterSketchComponent> Component;
	TSharedPtr<FPCGExSketchComponentEditTarget> Target;
	TSharedPtr<FPCGExSketchEditController> Controller;
	/** Subscription to the controller's OnChanged; this is what makes a binding the last-interacted one. */
	FDelegateHandle ChangedHandle;
};

/**
 * In-level authoring for Cluster Sketch components -- the Geometry/brush-mode bargain: enter the mode,
 * select actors, and every sketch component they carry becomes editable wherever it sits in the level.
 *
 * Deliberately owns NO editing or input logic. The gesture vocabulary is UPCGExSketchInputBinder and
 * the editing is FPCGExSketchEditController, both shared verbatim with the standalone Cluster Sketch
 * editor -- one bug fixed, fixed in both; one gesture improved, improved in both. This mode supplies
 * only host policy: which components are live (the selection), which one a ray addresses (nearest hit),
 * and drawing through the tools-context render hook.
 *
 * While the mode is active each bound component SUPPRESSES its own passive visual, so exactly one
 * drawer is live at any time and lines never double up.
 */
UCLASS()
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API UPCGExSketchEditorMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	static const FEditorModeID ModeID;

	UPCGExSketchEditorMode();

	//~ Begin UEdMode
	virtual void Enter() override;
	virtual void Exit() override;
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;

#if PCGEX_ENGINE_VERSION >= 508
	/** False, or the mode manager disables editor gizmos and moving the host actor becomes impossible.
	 *  The hook only exists from 5.8; before it, nothing gated those interactions. */
	virtual bool RequiresLegacyViewportInteractions() const override
	{
		return false;
	}
#endif
	//~ End UEdMode

	//~ Panel host seam. The mode holds N bindings; the panel edits exactly one at a time.
	/** Last-interacted binding's controller, falling back to the first bound one. */
	TSharedPtr<FPCGExSketchEditController> GetActiveController() const;
	/** Component behind GetActiveController -- what the panel's write-backs Modify(). */
	UObject* GetActiveSketchObject() const;
	void EnumerateSketches(TArray<FPCGExSketchPanelEntry>& OutEntries) const;
	void SetActiveController(const TSharedPtr<FPCGExSketchEditController>& InController);
	/** The mode draws through the tools-context render hook, which only runs on a realtime viewport. */
	void RequestViewportRefresh() const;

protected:
	/** Installs the sketch panel toolkit; the base FModeToolkit hosts only its own (empty) details views. */
	virtual void CreateToolkit() override;

private:
	const FPCGExSketchModeBinding* GetActiveBinding() const;

	void OnRenderCallback(IToolsContextRenderAPI* RenderAPI);
	void OnSelectionChanged(UObject* Object);
	/** A construction-script rerun (every undo on a Blueprint actor) or a reinstancing replaces the bound
	 *  components WITHOUT a selection event; the bindings follow the replacement so the controller and
	 *  its selection survive. */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNew);

	/** Rebuild bindings from the current actor selection (and re-apply visual suppression). */
	void RebuildBindings();
	void ReleaseBindings();
	/** Unsubscribe a set of bindings and hand their components the passive visual back. */
	void ReleaseBindingsIn(const TArray<FPCGExSketchModeBinding>& InBindings);

	/** Controller a ray addresses: nearest hit, else the one holding a selection, else the only one. */
	TSharedPtr<FPCGExSketchEditController> ResolveController(const FRay& WorldRay) const;

	TArray<FPCGExSketchModeBinding> Bindings;

	/** Which sketch the panel speaks for. Driven by the controllers' OnChanged, so it follows what the
	 *  user ACTED on -- hover alone must not swing the panel between overlapping sketches. */
	TWeakPtr<FPCGExSketchEditController> LastInteracted;

	UPROPERTY()
	TObjectPtr<UPCGExSketchInputBinder> InputBinder;

	FDelegateHandle OnRenderHandle;
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnObjectsReplacedHandle;
};
