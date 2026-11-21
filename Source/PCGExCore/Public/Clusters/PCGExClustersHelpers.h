// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExEdge.h"
#include "PCGExNode.h"
#include "Data/PCGExDataCommon.h"

class UPCGMetadata;

namespace PCGExData
{
	class FPointIO;
}

namespace PCGExClusters::Helpers
{
	using PCGExGraphs::FLink;
	using PCGExGraphs::FEdge;

	PCGEXCORE_API void SetClusterVtx(const TSharedPtr<PCGExData::FPointIO>& IO, PCGExDataId& OutId);
	PCGEXCORE_API void MarkClusterVtx(const TSharedPtr<PCGExData::FPointIO>& IO, const PCGExDataId& Id);
	PCGEXCORE_API void MarkClusterEdges(const TSharedPtr<PCGExData::FPointIO>& IO, const PCGExDataId& Id);
	PCGEXCORE_API void MarkClusterEdges(const TArrayView<TSharedRef<PCGExData::FPointIO>> Edges, const PCGExDataId& Id);
	PCGEXCORE_API void CleanupClusterTags(const TSharedPtr<PCGExData::FPointIO>& IO, const bool bKeepPairTag = false);

	/** Appends data tags to a spawned object's tags, unique, without the cluster id: a per-session object id that would change the saved object on every run. */
	PCGEXCORE_API void AppendSavedTags(const TSet<FString>& InTags, TArray<FName>& OutTags);

	PCGEXCORE_API bool IsPointDataVtxReady(const UPCGMetadata* Metadata);
	PCGEXCORE_API bool IsPointDataEdgeReady(const UPCGMetadata* Metadata);
	PCGEXCORE_API void CleanupVtxData(const TSharedPtr<PCGExData::FPointIO>& PointIO);
	PCGEXCORE_API void CleanupEdgeData(const TSharedPtr<PCGExData::FPointIO>& PointIO);
	PCGEXCORE_API void CleanupClusterData(const TSharedPtr<PCGExData::FPointIO>& PointIO);

	PCGEXCORE_API void GetAdjacencyData(const FCluster* InCluster, FNode& InNode, TArray<FAdjacencyData>& OutData);

	PCGEXCORE_API TSharedPtr<FCluster> TryGetCachedCluster(const TSharedRef<PCGExData::FPointIO>& VtxIO, const TSharedRef<PCGExData::FPointIO>& EdgeIO);
}
