// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/PCGExClusterCommon.h"
#include "Clusters/PCGExCluster.h"

int32 FPCGExNodeSelectionDetails::PickClosestNode(const PCGExClusters::FCluster& InCluster, const FVector& TargetPosition) const
{
	if (!WithinBounds(InCluster.Bounds, TargetPosition))
	{
		return -1;
	}

	const int32 NodeIndex = InCluster.FindClosestNode(TargetPosition, PickingMethod);
	if (NodeIndex == -1)
	{
		return -1;
	}

	return WithinDistance(InCluster.GetPos(NodeIndex), TargetPosition) ? NodeIndex : -1;
}
