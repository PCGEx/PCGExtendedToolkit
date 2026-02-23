// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Clusters/PCGExClusterCache.h"

namespace PCGExClusters
{
	class FPlanarFaceEnumerator;

	/**
	 * Cached per-node tangent frames for LocalTangent projection.
	 * Each entry is a quaternion whose Z axis is the node's estimated surface normal.
	 */
	class PCGEXGRAPHS_API FCachedTangentFrames : public ICachedClusterData
	{
	public:
		TSharedPtr<TArray<FQuat>> NodeTangentFrames; // node-indexed
	};

	/**
	 * Cached face enumerator data.
	 * Stores a pre-built or opportunistically cached FPlanarFaceEnumerator.
	 */
	class PCGEXGRAPHS_API FCachedFaceEnumerator : public ICachedClusterData
	{
	public:
		TSharedPtr<FPlanarFaceEnumerator> Enumerator;

		/** Owned projected positions (for pre-build where positions aren't stored elsewhere) */
		TSharedPtr<TArray<FVector2D>> ProjectedPositions;
	};

	/**
	 * Factory for pre-building face enumerator cache.
	 */
	class PCGEXGRAPHS_API FFaceEnumeratorCacheFactory : public IClusterCacheFactory
	{
	public:
		static inline const FName CacheKey = FName("FaceEnumerator");

		virtual FName GetCacheKey() const override { return CacheKey; }
		virtual FText GetDisplayName() const override;
		virtual FText GetTooltip() const override;
		virtual EClusterCacheType GetCacheType() const override { return EClusterCacheType::PreBuild; }

		virtual TSharedPtr<ICachedClusterData> Build(const FClusterCacheBuildContext& Context) const override;

		/** Compute a hash from projection settings for cache validation */
		static uint32 ComputeProjectionHash(const FPCGExGeo2DProjectionDetails& Projection);
	};
}
