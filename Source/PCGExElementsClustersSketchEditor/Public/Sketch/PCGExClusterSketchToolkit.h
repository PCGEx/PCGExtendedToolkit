// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Tools/BaseAssetToolkit.h"

class AActor;
class FAdvancedPreviewScene;
class FPCGExSketchAssetEditTarget;
class FPCGExSketchEditController;
class UPCGExClusterSketch;
class UPCGExClusterSketchComponent;

/**
 * The Cluster Sketch editor toolkit: FBaseAssetToolkit's viewport + details pair over an advanced
 * preview scene, with the edit controller shared between this host and any future one.
 *
 * Wiring rules inherited from the verified engine recipe:
 *  - the mode manager MUST get SetPreviewScene() or ITF-spawned actors land in the level editor world;
 *  - the viewport tab must be invoked + focused in PostInitAssetEditor or it never ticks;
 *  - the base CreateEditorViewportClient leaks a raw FPreviewScene -- always overridden here.
 */
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API FPCGExClusterSketchToolkit : public FBaseAssetToolkit
{
public:
	explicit FPCGExClusterSketchToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FPCGExClusterSketchToolkit() override;

	//~ FAssetEditorToolkit
	virtual FName GetToolkitFName() const override { return FName("PCGExClusterSketchEditor"); }
	virtual FText GetBaseToolkitName() const override { return INVTEXT("Cluster Sketch Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Cluster Sketch"); }
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.1f, 0.75f, 0.65f); }
	virtual void PostInitAssetEditor() override;

	//~ FBaseAssetToolkit
	virtual void CreateWidgets() override;
	virtual void CreateEditorModeManager() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;

	TSharedPtr<FPCGExSketchEditController> GetController() const { return Controller; }

private:
	/** Give the preview scene a real sketch component, so this editor renders through the SAME mesh
	 *  layer the in-level mode does instead of an immediate-mode lookalike. Needs an owning actor: the
	 *  component parents its instanced-mesh children to GetOwner(). */
	void CreatePreviewSketch(UPCGExClusterSketch* InSketch);

	TSharedPtr<FAdvancedPreviewScene> ObjectScene;
	TSharedPtr<FPCGExSketchAssetEditTarget> EditTarget;
	TSharedPtr<FPCGExSketchEditController> Controller;

	/** Owned by the preview world, which roots them; weak so teardown order cannot matter. */
	TWeakObjectPtr<AActor> PreviewActor;
	TWeakObjectPtr<UPCGExClusterSketchComponent> PreviewComponent;
};
