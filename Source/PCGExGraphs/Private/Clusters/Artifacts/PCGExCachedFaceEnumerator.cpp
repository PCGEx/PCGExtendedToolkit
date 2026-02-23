// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/Artifacts/PCGExCachedFaceEnumerator.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Clusters/PCGExCluster.h"
#include "Math/PCGExProjectionDetails.h"

#define LOCTEXT_NAMESPACE "PCGExCachedFaceEnumerator"

namespace PCGExClusters
{
#pragma region FFaceEnumeratorCacheFactory

	FText FFaceEnumeratorCacheFactory::GetDisplayName() const
	{
		return LOCTEXT("DisplayName", "Face Enumerator");
	}

	FText FFaceEnumeratorCacheFactory::GetTooltip() const
	{
		return LOCTEXT("Tooltip", "Pre-built DCEL-based planar face enumerator for cell-finding operations.");
	}

	TSharedPtr<ICachedClusterData> FFaceEnumeratorCacheFactory::Build(const FClusterCacheBuildContext& Context) const
	{
		if (!Context.Projection)
		{
			// No projection settings provided - can't build
			return nullptr;
		}

		// Build the face enumerator using projection details
		// This internally builds node-indexed positions
		TSharedPtr<FPlanarFaceEnumerator> Enumerator = MakeShared<FPlanarFaceEnumerator>();
		Enumerator->Build(Context.Cluster, *Context.Projection);

		if (!Enumerator->IsBuilt())
		{
			return nullptr;
		}

		// Create cached data
		TSharedPtr<FCachedFaceEnumerator> Cached = MakeShared<FCachedFaceEnumerator>();
		Cached->ContextHash = ComputeProjectionHash(*Context.Projection);
		Cached->Enumerator = Enumerator;
		// Enumerator owns the node-indexed positions
		Cached->ProjectedPositions = Enumerator->GetProjectedPositions();

		return Cached;
	}

	uint32 FFaceEnumeratorCacheFactory::ComputeProjectionHash(const FPCGExGeo2DProjectionDetails& Projection)
	{
		// Hash the projection settings that affect the 2D layout
		uint32 Hash = GetTypeHash(Projection.Method);

		// LocalTangent: topology is fully determined by cluster geometry, no projection params to hash
		if (Projection.Method == EPCGExProjectionMethod::LocalTangent) { return Hash; }

		Hash = HashCombine(Hash, GetTypeHash(Projection.Normal.X));
		Hash = HashCombine(Hash, GetTypeHash(Projection.Normal.Y));
		Hash = HashCombine(Hash, GetTypeHash(Projection.Normal.Z));
		return Hash;
	}

#pragma endregion
}

#undef LOCTEXT_NAMESPACE
