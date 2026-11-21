// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineTrajanDFS.h"

#pragma region FPCGExEdgeRefineTrajanDFS

void FPCGExEdgeRefineTrajanDFS::Process()
{
	const int32 NumNodes = Cluster->Nodes->Num();

	TArray<int32> Disc;   // discovery time
	TArray<int32> Low;    // lowest reachable ancestor
	TArray<int32> Parent; // DFS parent

	Disc.Init(-1, NumNodes);
	Low.Init(-1, NumNodes);
	Parent.Init(-1, NumNodes);

	int32 Time = 0;

	TArray<int32> Bridges;
	Bridges.Reserve(Cluster->Edges->Num());


	TFunction<void(int32)> DFS = [&](const int32 Index)
	{
		const PCGExClusters::FNode& Current = *Cluster->GetNode(Index);

		Disc[Index] = Low[Index] = Time++;

		for (const PCGExGraphs::FLink Lk : Current.Links)
		{
			if (Disc[Lk.Node] == -1)
			{
				Parent[Lk.Node] = Index;

				DFS(Lk.Node);

				Low[Index] = FMath::Min(Low[Index], Low[Lk.Node]);

				if (Low[Lk.Node] > Disc[Index])
				{
					Bridges.Add(Lk.Edge);
				}
			}
			else if (Lk.Node != Parent[Index])
			{
				Low[Index] = FMath::Min(Low[Index], Disc[Lk.Node]);
			}
		}
	};

	for (int32 i = 0; i < NumNodes; ++i)
	{
		if (Disc[i] == -1)
		{
			DFS(i);
		}
	}
	for (const int32 BridgeIndex : Bridges)
	{
		Cluster->GetEdge(BridgeIndex)->bValid = bInvert;
	}
}

#pragma endregion
