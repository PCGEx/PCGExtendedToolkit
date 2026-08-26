// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineRemoveLeaves.h"

#pragma region FPCGExEdgeRemoveLeaves

void FPCGExEdgeRemoveLeaves::ProcessNode(PCGExClusters::FNode& Node)
{
	if (!Node.IsLeaf())
	{
		return;
	}

	int32 CurrentNodeIndex = Node.Index;
	int32 PrevNodeIndex = -1;

	while (CurrentNodeIndex != -1)
	{
		PCGExClusters::FNode* From = Cluster->GetNode(CurrentNodeIndex);

		if (From->IsComplex())
		{
			// Junction: it anchors the chain and keeps its remaining edges.
			return;
		}

		// Step onto the neighbour we did not arrive from. Every node walked is degree 1 or 2, so a
		// chain reachable from a leaf cannot close on itself and the walk always terminates.
		int32 NextNodeIndex = -1;
		int32 NextEdgeIndex = -1;
		for (const PCGExGraphs::FLink& Lk : From->Links)
		{
			if (Lk.Node != PrevNodeIndex)
			{
				NextNodeIndex = Lk.Node;
				NextEdgeIndex = Lk.Edge;
				break;
			}
		}

		From->bValid = false;

		if (NextNodeIndex == -1)
		{
			return;
		}

		Cluster->GetEdge(NextEdgeIndex)->bValid = false;

		PrevNodeIndex = CurrentNodeIndex;
		CurrentNodeIndex = NextNodeIndex;
	}
}

#pragma endregion
