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

/** One editable sketch: the component, its edit target, and the controller driving it. */
struct FPCGExSketchModeBinding
{
	TWeakObjectPtr<UPCGExClusterSketchComponent> Component;
	TSharedPtr<FPCGExSketchComponentEditTarget> Target;
	TSharedPtr<FPCGExSketchEditController> Controller;
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
class PCGEXGRAPHSEDITOR_API UPCGExSketchEditorMode : public UBaseLegacyWidgetEdMode
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
	virtual bool RequiresLegacyViewportInteractions() const override { return false; }
#endif
	//~ End UEdMode

private:
	void OnRenderCallback(IToolsContextRenderAPI* RenderAPI);
	void OnSelectionChanged(UObject* Object);

	/** Rebuild bindings from the current actor selection (and re-apply visual suppression). */
	void RebuildBindings();
	void ReleaseBindings();
	/** Hand a set of bindings' components their passive visual back. */
	void ReleaseBindingsIn(const TArray<FPCGExSketchModeBinding>& InBindings);

	/** Controller a ray addresses: nearest hit, else the one holding a selection, else the only one. */
	TSharedPtr<FPCGExSketchEditController> ResolveController(const FRay& WorldRay) const;

	TArray<FPCGExSketchModeBinding> Bindings;

	UPROPERTY()
	TObjectPtr<UPCGExSketchInputBinder> InputBinder;

	FDelegateHandle OnRenderHandle;
	FDelegateHandle OnSelectionChangedHandle;
};
