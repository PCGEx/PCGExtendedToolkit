// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FPCGExSketchEditController;
class UPCGExClusterSketchComponent;
class UPCGExSketchInputBinder;

/**
 * Preview-scene viewport client for the standalone Cluster Sketch editor.
 *
 * Owns NO editing, input or drawing logic: the gesture vocabulary lives in UPCGExSketchInputBinder,
 * the editing in FPCGExSketchEditController, and the render pass in FPCGExSketchDrawHelper -- all
 * shared with the in-level editor mode, so a fix or an improvement in any of them reaches both hosts.
 * This class only supplies host policy (one fixed controller, one preview-scene sketch component) and
 * hosts the binder on the mode manager's always-live ITF router.
 */
class PCGEXGRAPHSEDITOR_API FPCGExClusterSketchViewportClient final : public FEditorViewportClient
{
public:
	FPCGExClusterSketchViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TSharedPtr<FPCGExSketchEditController>& InController, UPCGExClusterSketchComponent* InSketchComponent);
	virtual ~FPCGExClusterSketchViewportClient() override;

	//~ FEditorViewportClient
	/** Dark preview backdrop. Overridden rather than set on the preview scene: the scene's color comes
	 *  from the shared editor-wide UAssetViewerSettings profile, which we must not write to. */
	virtual FLinearColor GetBackgroundColor() const override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	/** Alt+LMB never reaches the ITF router (camera tracking suppresses tools-context routing), so the
	 *  Alt+click delete rides the legacy click path -- the level mode does the same from HandleClick. */
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

private:
	TSharedPtr<FPCGExSketchEditController> Controller;
	FEditorModeTools* OwnerModeTools = nullptr;

	/** The mesh layer, living in the preview scene. Null degrades to pure immediate-mode drawing. */
	TWeakObjectPtr<UPCGExClusterSketchComponent> SketchComponent;

	TObjectPtr<UPCGExSketchInputBinder> InputBinder;
};
