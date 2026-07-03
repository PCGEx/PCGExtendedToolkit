// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"

#include "PCGExCollectionThumbnailRenderer.generated.h"

class FCanvas;
class FRenderTarget;
class UPCGExAssetCollection;
class UTexture2D;

/**
 * Thumbnail renderer for all UPCGExAssetCollection types (registered on the base class by the
 * module). Draws an adaptive mosaic of the entries in authored order (1 / 2x2 / 3x3 by count);
 * past 9 entries the last cell shows a "+N" overflow count.
 *
 * Per-cell resolution, cheapest-first: nested subcollection -> recursive mosaic (depth-capped);
 * cached package thumbnail (no asset load); loaded child's own renderer (delegated sub-rect);
 * placeholder.
 *
 * OnPropertyChange frequency: the pool re-renders on edit and the editor bakes the result into the
 * package thumbnail on save, so unloaded assets show the mosaic in the content browser too.
 */
UCLASS()
class PCGEXCOLLECTIONSEDITOR_API UPCGExCollectionThumbnailRenderer : public UThumbnailRenderer
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

protected:
	/** Nested subcollection cells recurse this many levels before falling back to a placeholder. */
	static constexpr int32 MaxRecursionDepth = 1;

	void DrawCollection(const UPCGExAssetCollection* Collection, float X, float Y, float Width, float Height, FRenderTarget* Viewport, FCanvas* Canvas, int32 Depth);
	void DrawCell(const FSoftObjectPath& AssetPath, float X, float Y, float Width, float Height, FRenderTarget* Viewport, FCanvas* Canvas, int32 Depth);
	void DrawPlaceholder(float X, float Y, float Width, float Height, FCanvas* Canvas, const FLinearColor& Color) const;
	void DrawOverflowCell(int32 OverflowCount, float X, float Y, float Width, float Height, FCanvas* Canvas) const;

	/**
	 * Transient texture from an asset's cached package thumbnail (no asset load), or nullptr if none.
	 * Cache-first (consulted before any load); entries invalidated via OnThumbnailDirtied.
	 */
	UTexture2D* GetOrBuildCachedThumbnailTexture(const FSoftObjectPath& AssetPath);

	/** Insert (or negative-cache with nullptr), dropping the whole cache first if it is full. */
	void StoreCellTexture(const FSoftObjectPath& ResolvedPath, UTexture2D* Texture);

	/** Evict the cached cell texture for a dirtied asset so the next render rebuilds it. */
	void OnThumbnailDirtied(const FSoftObjectPath& AssetPath);

private:
	/** Subscribe to UThumbnailManager::OnThumbnailDirtied exactly once (lazy, on first build). */
	void EnsureThumbnailDirtyListener();

	/** Soft cap on cached cell textures; when exceeded the cache is dropped wholesale (cells
	 *  are cheap to rebuild on demand), bounding editor-session memory growth. */
	static constexpr int32 MaxCacheEntries = 256;

	/** Cell textures keyed by resolved asset path. A null value is a negative-cache marker
	 *  ("this asset has no usable cached thumbnail") so repeat renders skip the package read. */
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<UTexture2D>> CellTextureCache;

	bool bThumbnailDirtiedBound = false;
};
