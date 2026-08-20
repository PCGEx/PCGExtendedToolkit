// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"

#include "PCGExClusterSketchThumbnailRenderer.generated.h"

class FCanvas;
class FRenderTarget;

/**
 * Draws a Cluster Sketch's actual topology: the graph projected through a fixed three-quarter camera,
 * fitted to the frame, in the editor's own palette on its own backdrop -- so a thumbnail reads as a
 * small view of the sketch editor.
 *
 * Canvas rather than a preview scene, deliberately: at thumbnail scale a graph reads far better as
 * clean lines and dots than as shaded markers, and this needs no scene, lighting or resolvable mesh
 * assets. Colours come from the shared style settings, so it restyles along with both editing hosts.
 *
 * OnPropertyChange frequency: re-renders on edit, and the editor bakes the result into the package
 * thumbnail on save, so unloaded sketches show their topology in the content browser too.
 */
UCLASS()
class PCGEXELEMENTSCLUSTERSSKETCHEDITOR_API UPCGExClusterSketchThumbnailRenderer : public UThumbnailRenderer
{
	GENERATED_BODY()

public:
	//~ Begin UThumbnailRenderer
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily) override;

	virtual EThumbnailRenderFrequency GetThumbnailRenderFrequency(UObject* Object) const override
	{
		return EThumbnailRenderFrequency::OnPropertyChange;
	}

	//~ End UThumbnailRenderer
};
