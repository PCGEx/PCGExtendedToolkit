// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompGridPartition.h"

#include "Core/PCGExDecompositionUtils.h"

#pragma region FPCGExDecompGridPartition

bool FPCGExDecompGridPartition::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() == 0)
	{
		return false;
	}

	const int32 NumNodes = Cluster->Nodes->Num();

	// Ensure valid cell size
	const FVector SafeCellSize = FVector(
		FMath::Max(CellSize.X, KINDA_SMALL_NUMBER),
		FMath::Max(CellSize.Y, KINDA_SMALL_NUMBER),
		FMath::Max(CellSize.Z, KINDA_SMALL_NUMBER));

	// Compute bounding box
	FBox Bounds(ForceInit);
	for (int32 i = 0; i < NumNodes; i++)
	{
		if (!Cluster->GetNode(i)->bValid)
		{
			continue;
		}
		Bounds += Cluster->GetPos(i);
	}

	const FVector BoundsMin = Bounds.Min;

	// Cache per-node grid coords when cell sizes are requested, so the size pass
	// below reuses them instead of recomputing the quantization.
	TArray<FIntVector> GridCoords;
	if (OutResult.bWantsCellSizes)
	{
		// Zeroed, not Uninitialized: invalid nodes never write their slot; a defined value
		// removes the latent uninitialized-read hazard even though the size pass skips them.
		GridCoords.SetNumZeroed(NumNodes);
	}

	// Quantize each node position to a grid cell
	TMap<FIntVector, int32> CellMap;      // GridCoord -> CellID
	TMap<int32, TArray<int32>> CellNodes; // CellID -> NodeIndices

	int32 NextCellID = 0;

	for (int32 i = 0; i < NumNodes; i++)
	{
		if (!Cluster->GetNode(i)->bValid)
		{
			OutResult.NodeCellIDs[i] = -1;
			continue;
		}

		const FVector Pos = Cluster->GetPos(i);
		const FIntVector GridCoord = FIntVector(
			FMath::FloorToInt((Pos.X - BoundsMin.X) / SafeCellSize.X),
			FMath::FloorToInt((Pos.Y - BoundsMin.Y) / SafeCellSize.Y),
			FMath::FloorToInt((Pos.Z - BoundsMin.Z) / SafeCellSize.Z));

		if (OutResult.bWantsCellSizes)
		{
			GridCoords[i] = GridCoord;
		}

		int32* ExistingID = CellMap.Find(GridCoord);
		int32 CellID;
		if (ExistingID)
		{
			CellID = *ExistingID;
		}
		else
		{
			CellID = NextCellID++;
			CellMap.Add(GridCoord, CellID);
		}

		OutResult.NodeCellIDs[i] = CellID;
		CellNodes.FindOrAdd(CellID).Add(i);
	}

	// Merge under-populated cells (fewer than MinNodesPerCell nodes) into a neighbour so the
	// threshold is respected; MergeMode picks the target (see EPCGExDecompGridMergeMode).
	bool bAnyMerged = false;
	if (MinNodesPerCell > 1 && CellNodes.Num() > 1)
	{
		if (MergeMode == EPCGExDecompGridMergeMode::Adjacent)
		{
			// Targets come from each cell's grid-coord neighbours (26-conn), not a full-cell scan, so
			// the pass is O(N). A spatially isolated cell (no occupied neighbour) stays its own cell.
			TArray<FIntVector> Offsets;
			Offsets.Reserve(26);
			for (int32 Dz = -1; Dz <= 1; Dz++)
			{
				for (int32 Dy = -1; Dy <= 1; Dy++)
				{
					for (int32 Dx = -1; Dx <= 1; Dx++)
					{
						if (Dx != 0 || Dy != 0 || Dz != 0)
						{
							Offsets.Add(FIntVector(Dx, Dy, Dz));
						}
					}
				}
			}

			// Cell -> its grid coords (CellMap is the inverse coord -> cell lookup, mutated on merge).
			TMap<int32, TArray<FIntVector>> CellCoords;
			CellCoords.Reserve(CellNodes.Num());
			for (const TPair<FIntVector, int32>& Pair : CellMap)
			{
				CellCoords.FindOrAdd(Pair.Value).Add(Pair.Key);
			}

			TArray<int32> Queue;
			for (const auto& Pair : CellNodes)
			{
				if (Pair.Value.Num() < MinNodesPerCell)
				{
					Queue.Add(Pair.Key);
				}
			}
			Queue.Sort(); // Deterministic processing order (CellNodes/CellMap iteration is unstable).

			int32 Head = 0;
			while (Head < Queue.Num())
			{
				const int32 SmallID = Queue[Head++];
				const TArray<int32>* SmallNodes = CellNodes.Find(SmallID);
				if (!SmallNodes || SmallNodes->Num() >= MinNodesPerCell)
				{
					continue;
				} // Merged away, or grew past the threshold via incoming merges

				// Largest grid-adjacent cell; tie-break by lowest CellID for determinism.
				int32 BestTargetID = -1;
				int32 BestTargetCount = -1;
				for (const FIntVector& Coord : CellCoords[SmallID])
				{
					for (const FIntVector& Off : Offsets)
					{
						const int32* Neighbor = CellMap.Find(Coord + Off);
						if (!Neighbor || *Neighbor == SmallID)
						{
							continue;
						}
						const int32 NeighborCount = CellNodes[*Neighbor].Num();
						if (BestTargetID < 0 || NeighborCount > BestTargetCount ||
							(NeighborCount == BestTargetCount && *Neighbor < BestTargetID))
						{
							BestTargetCount = NeighborCount;
							BestTargetID = *Neighbor;
						}
					}
				}

				if (BestTargetID < 0)
				{
					continue;
				} // Spatially isolated -- leave as its own cell

				// Absorb the small cell into the target and reassign its coords.
				const TArray<int32> NodesToMove = *SmallNodes; // Copy before mutation
				for (const int32 NodeIdx : NodesToMove)
				{
					OutResult.NodeCellIDs[NodeIdx] = BestTargetID;
				}
				CellNodes[BestTargetID].Append(NodesToMove);

				const TArray<FIntVector> CoordsToMove = CellCoords[SmallID]; // Copy before mutation
				for (const FIntVector& Coord : CoordsToMove)
				{
					CellMap[Coord] = BestTargetID;
				}
				CellCoords[BestTargetID].Append(CoordsToMove);

				CellNodes.Remove(SmallID);
				CellCoords.Remove(SmallID);
				bAnyMerged = true;

				if (CellNodes[BestTargetID].Num() < MinNodesPerCell)
				{
					Queue.Add(BestTargetID);
				}
			}
		}
		else
		{
			// Nearest merge. Running per-cell position sums keep centroids O(1) to query/update.
			// O(cells^2) overall -- fine for moderate cell counts; use Adjacent for very fine grids.
			TMap<int32, FVector> CellPosSum;
			CellPosSum.Reserve(CellNodes.Num());
			for (const auto& Pair : CellNodes)
			{
				FVector Sum = FVector::ZeroVector;
				for (const int32 NodeIdx : Pair.Value)
				{
					Sum += Cluster->GetPos(NodeIdx);
				}
				CellPosSum.Add(Pair.Key, Sum);
			}

			while (CellNodes.Num() > 1)
			{
				// Most under-populated cell; tie-break by lowest CellID for determinism.
				int32 SmallestID = -1;
				int32 SmallestCount = 0;
				for (const auto& Pair : CellNodes)
				{
					const int32 Count = Pair.Value.Num();
					if (Count >= MinNodesPerCell)
					{
						continue;
					}
					if (SmallestID < 0 || Count < SmallestCount ||
						(Count == SmallestCount && Pair.Key < SmallestID))
					{
						SmallestCount = Count;
						SmallestID = Pair.Key;
					}
				}

				if (SmallestID < 0)
				{
					break;
				} // Every cell meets the threshold

				const FVector SmallCentroid = CellPosSum[SmallestID] / SmallestCount;

				// Nearest other cell by centroid distance; tie-break by lowest CellID.
				int32 BestTargetID = -1;
				double BestDistSq = TNumericLimits<double>::Max();
				for (const auto& Pair : CellNodes)
				{
					if (Pair.Key == SmallestID)
					{
						continue;
					}
					const FVector TargetCentroid = CellPosSum[Pair.Key] / Pair.Value.Num();
					const double DistSq = FVector::DistSquared(SmallCentroid, TargetCentroid);
					if (BestTargetID < 0 || DistSq < BestDistSq ||
						(DistSq == BestDistSq && Pair.Key < BestTargetID))
					{
						BestDistSq = DistSq;
						BestTargetID = Pair.Key;
					}
				}

				if (BestTargetID < 0)
				{
					break;
				} // No target available (only reachable with a single cell)

				// Absorb the small cell into the target, then drop it.
				const TArray<int32> NodesToMove = CellNodes[SmallestID]; // Copy before mutation
				for (const int32 NodeIdx : NodesToMove)
				{
					OutResult.NodeCellIDs[NodeIdx] = BestTargetID;
				}
				CellNodes[BestTargetID].Append(NodesToMove);
				CellPosSum[BestTargetID] += CellPosSum[SmallestID];

				CellNodes.Remove(SmallestID);
				CellPosSum.Remove(SmallestID);
				bAnyMerged = true;
			}
		}
	}

	if (bAnyMerged)
	{
		// Merging left gaps in the CellID space; close them. Skipped otherwise -- IDs stay dense.
		NextCellID = PCGExDecomposition::CompactCellIDs(OutResult.NodeCellIDs);
	}

	if (OutResult.bWantsCellSizes)
	{
		// Box-like: each cell's size is its grid-coord AABB span times the cell size.
		PCGExDecomposition::AccumulateQuantizedCellSizes(
			NumNodes, NextCellID, SafeCellSize,
			[&OutResult](const int32 i) { return OutResult.NodeCellIDs[i]; },
			[&GridCoords](const int32 i) { return GridCoords[i]; },
			OutResult.CellSizes);
	}

	OutResult.NumCells = NextCellID;
	return OutResult.NumCells > 0;
}

#pragma endregion

#pragma region UPCGExDecompGridPartition

#pragma endregion
