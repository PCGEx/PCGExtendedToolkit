// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompGridPartition.h"

#pragma region FPCGExDecompGridPartition

bool FPCGExDecompGridPartition::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() == 0) { return false; }

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
		if (!Cluster->GetNode(i)->bValid) { continue; }
		Bounds += Cluster->GetPos(i);
	}

	const FVector BoundsMin = Bounds.Min;

	// Quantize each node position to a grid cell
	TMap<FIntVector, int32> CellMap; // GridCoord -> CellID
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

	// Merge underpopulated cells into nearest neighbor
	if (MinNodesPerCell > 1)
	{
		bool bMergedAny = true;
		while (bMergedAny)
		{
			bMergedAny = false;

			TArray<int32> SmallCells;
			for (const auto& Pair : CellNodes)
			{
				if (Pair.Value.Num() < MinNodesPerCell) { SmallCells.Add(Pair.Key); }
			}

			if (SmallCells.Num() == 0 || SmallCells.Num() == CellNodes.Num()) { break; } // All small or none small — stop

			for (const int32 SmallCellID : SmallCells)
			{
				const TArray<int32>* NodesPtr = CellNodes.Find(SmallCellID);
				if (!NodesPtr || NodesPtr->Num() == 0) { continue; } // Already merged away

				FVector SmallCentroid = FVector::ZeroVector;
				for (const int32 NodeIdx : *NodesPtr) { SmallCentroid += Cluster->GetPos(NodeIdx); }
				SmallCentroid /= NodesPtr->Num();

				int32 BestTargetID = -1;
				double BestDistSq = MAX_dbl;

				for (const auto& Pair : CellNodes)
				{
					if (Pair.Key == SmallCellID) { continue; }

					FVector TargetCentroid = FVector::ZeroVector;
					for (const int32 NodeIdx : Pair.Value) { TargetCentroid += Cluster->GetPos(NodeIdx); }
					TargetCentroid /= Pair.Value.Num();

					const double DistSq = FVector::DistSquared(SmallCentroid, TargetCentroid);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestTargetID = Pair.Key;
					}
				}

				if (BestTargetID >= 0)
				{
					const TArray<int32> NodesToMove = *NodesPtr; // Copy before mutation
					for (const int32 NodeIdx : NodesToMove) { OutResult.NodeCellIDs[NodeIdx] = BestTargetID; }
					CellNodes[BestTargetID].Append(NodesToMove);
					CellNodes.Remove(SmallCellID);
					bMergedAny = true;
				}
			}
		}

		// Re-compact CellIDs to be sequential
		TMap<int32, int32> Remap;
		int32 CompactID = 0;
		for (const auto& Pair : CellNodes) { Remap.Add(Pair.Key, CompactID++); }

		for (int32& ID : OutResult.NodeCellIDs)
		{
			if (ID < 0) { continue; }
			if (const int32* Remapped = Remap.Find(ID)) { ID = *Remapped; }
		}

		NextCellID = CompactID;
	}

	OutResult.NumCells = NextCellID;
	return OutResult.NumCells > 0;
}

#pragma endregion

#pragma region UPCGExDecompGridPartition

void UPCGExDecompGridPartition::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompGridPartition* TypedOther = Cast<UPCGExDecompGridPartition>(Other))
	{
		CellSize = TypedOther->CellSize;
		MinNodesPerCell = TypedOther->MinNodesPerCell;
	}
}

#pragma endregion
