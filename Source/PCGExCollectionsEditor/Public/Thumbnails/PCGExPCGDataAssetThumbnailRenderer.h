// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/DefaultSizedThumbnailRenderer.h"

#include "PCGExPCGDataAssetThumbnailRenderer.generated.h"

class FCanvas;
class FRenderTarget;
class FPCGExDataAssetThumbnailScene;

/**
 * Thumbnail renderer for UPCGDataAsset (registered on the class by the module). Draws the asset's
 * "Meshes" pin as instanced static meshes in a preview scene: a level export, an assembly export or
 * any hand-made data asset carrying mesh points shows its geometry instead of the class icon.
 *
 * Mesh per point resolves the way the staging pipeline does -- entry hash through the asset's embedded
 * CollectionMap pin -- with the exporter's raw "Mesh" attribute as the fallback for assets exported
 * without collections. Points without a resolvable mesh are skipped; an asset with none is not
 * visualized (CanVisualizeAsset).
 *
 * OnPropertyChange frequency: an embedded export is a fresh object on every re-export, so a stale
 * cache can never outlive the content it shows.
 */
UCLASS()
class PCGEXCOLLECTIONSEDITOR_API UPCGExPCGDataAssetThumbnailRenderer : public UDefaultSizedThumbnailRenderer
{
	GENERATED_BODY()

public:
	//~ Begin UThumbnailRenderer
	virtual bool CanVisualizeAsset(UObject* Object) override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* Viewport, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	virtual EThumbnailRenderFrequency GetThumbnailRenderFrequency(UObject* Object) const override
	{
		return EThumbnailRenderFrequency::OnPropertyChange;
	}
	//~ End UThumbnailRenderer

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

private:
	/** Owned; created on first draw, torn down with the renderer (the engine's own renderers do the same). */
	FPCGExDataAssetThumbnailScene* ThumbnailScene = nullptr;
};
