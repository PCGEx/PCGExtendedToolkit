// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompMaxBoxes.h"

#pragma region FPCGExDecompMaxBoxes

bool FPCGExDecompMaxBoxes::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() == 0) { return false; }

	const int32 NumNodes = Cluster->Nodes->Num();

	// Resolve voxel size (auto-detect from edges or use manual)
	const FVector ResolvedVoxelSize = FPCGExDecompOccupancyGrid::ResolveVoxelSize(Cluster, VoxelSizeMode, VoxelSize);

	// Build occupancy grid
	FPCGExDecompOccupancyGrid Grid;
	if (!Grid.Build(Cluster, TransformSpace, ResolvedVoxelSize, CustomTransform)) { return false; }

	// Compute max extent in voxels from MaxCellSize (world units)
	const FIntVector MaxExtent = FIntVector(
		MaxCellSize.X > KINDA_SMALL_NUMBER ? FMath::Max(FMath::FloorToInt(MaxCellSize.X / ResolvedVoxelSize.X), 1) : MAX_int32,
		MaxCellSize.Y > KINDA_SMALL_NUMBER ? FMath::Max(FMath::FloorToInt(MaxCellSize.Y / ResolvedVoxelSize.Y), 1) : MAX_int32,
		MaxCellSize.Z > KINDA_SMALL_NUMBER ? FMath::Max(FMath::FloorToInt(MaxCellSize.Z / ResolvedVoxelSize.Z), 1) : MAX_int32);

	// Available = occupied and not yet claimed
	TBitArray<> Available = Grid.Occupied;

	int32 RemainingCount = 0;
	for (int32 i = 0; i < Grid.TotalVoxels; i++) { if (Available[i]) { RemainingCount++; } }

	// Per-voxel CellID
	TArray<int32> VoxelCellIDs;
	VoxelCellIDs.SetNumUninitialized(Grid.TotalVoxels);
	for (int32& ID : VoxelCellIDs) { ID = -1; }

	int32 NextCellID = 0;
	TArray<int32> CellVoxelCounts; // Track occupied voxel count per CellID

	// Iteratively extract the best box (compactness-scored when Balance > 0, pure volume otherwise)
	while (RemainingCount > 0)
	{
		FIntVector BoxMin, BoxMax;
		int32 BoxVolume = 0;

		if (!FindLargestBox(Grid, Available, BoxMin, BoxMax, BoxVolume) || BoxVolume == 0) { break; }

		SubdivideAndClaim(Grid, BoxMin, BoxMax, MaxExtent, Available, VoxelCellIDs, NextCellID, RemainingCount, CellVoxelCounts);
	}

	// Merge adjacent cells that together form a perfect box
	MergeAdjacentCells(Grid, VoxelCellIDs, NextCellID, MaxExtent);

	// Rebuild voxel counts after merge (CellIDs were re-compacted)
	CellVoxelCounts.Reset();
	CellVoxelCounts.SetNumZeroed(NextCellID);
	for (int32 i = 0; i < Grid.TotalVoxels; i++)
	{
		if (VoxelCellIDs[i] >= 0 && VoxelCellIDs[i] < NextCellID) { CellVoxelCounts[VoxelCellIDs[i]]++; }
	}

	// Discard cells below MinVoxelsPerCell (set their CellID to -1)
	if (MinVoxelsPerCell > 1)
	{
		for (int32 i = 0; i < Grid.TotalVoxels; i++)
		{
			if (VoxelCellIDs[i] >= 0 && VoxelCellIDs[i] < CellVoxelCounts.Num())
			{
				if (CellVoxelCounts[VoxelCellIDs[i]] < MinVoxelsPerCell)
				{
					VoxelCellIDs[i] = -1;
				}
			}
		}

		// Re-compact CellIDs to be sequential
		TMap<int32, int32> Remap;
		int32 CompactID = 0;
		for (int32 i = 0; i < Grid.TotalVoxels; i++)
		{
			if (VoxelCellIDs[i] < 0) { continue; }
			if (!Remap.Contains(VoxelCellIDs[i])) { Remap.Add(VoxelCellIDs[i], CompactID++); }
			VoxelCellIDs[i] = Remap[VoxelCellIDs[i]];
		}

		NextCellID = CompactID;
	}

	// Map voxel CellIDs back to node CellIDs (node-centric to handle multiple nodes per voxel)
	for (int32 i = 0; i < NumNodes; i++)
	{
		const int32 VoxelIdx = Grid.NodeToVoxelIndex[i];
		if (VoxelIdx >= 0 && VoxelCellIDs[VoxelIdx] >= 0)
		{
			OutResult.NodeCellIDs[i] = VoxelCellIDs[VoxelIdx];
		}
	}

	OutResult.NumCells = NextCellID;
	return OutResult.NumCells > 0;
}

bool FPCGExDecompMaxBoxes::FindLargestBox(
	const FPCGExDecompOccupancyGrid& Grid,
	const TBitArray<>& Available,
	FIntVector& OutMin,
	FIntVector& OutMax,
	int32& OutVolume) const
{
	const int32 GX = Grid.GridDimensions.X;
	const int32 GY = Grid.GridDimensions.Y;
	const int32 GZ = Grid.GridDimensions.Z;

	OutVolume = 0;
	double BestScore = -1.0;
	const bool bUseBalance = Balance > KINDA_SMALL_NUMBER;

	// ColAvail[x + y * GX] = true iff ALL z-layers from Z1 to current Z2 at (x,y) are available
	TArray<bool> ColAvail;
	ColAvail.SetNum(GX * GY);

	// Y-direction histogram: Hist[x] = consecutive Y rows where ColAvail is true
	TArray<int32> Hist;
	Hist.SetNum(GX);

	// Stack for the largest-rectangle-in-histogram algorithm
	TArray<TPair<int32, int32>> Stack; // (start_index, height)

	for (int32 Z1 = 0; Z1 < GZ; Z1++)
	{
		// Reset ColAvail for new Z1
		for (int32 i = 0; i < GX * GY; i++) { ColAvail[i] = true; }

		for (int32 Z2 = Z1; Z2 < GZ; Z2++)
		{
			const int32 ZDepth = Z2 - Z1 + 1;

			// AND in the Z2 layer: ColAvail stays true only if Z2 layer is also available
			for (int32 Y = 0; Y < GY; Y++)
			{
				for (int32 X = 0; X < GX; X++)
				{
					const int32 Idx2D = X + Y * GX;
					if (ColAvail[Idx2D])
					{
						ColAvail[Idx2D] = Available[Grid.FlatIndex(X, Y, Z2)];
					}
				}
			}

			// Find largest rectangle in the 2D ColAvail mask using histogram method
			// Reset Y-histogram
			for (int32 X = 0; X < GX; X++) { Hist[X] = 0; }

			for (int32 Y = 0; Y < GY; Y++)
			{
				// Update histogram: increment for available columns, reset for unavailable
				for (int32 X = 0; X < GX; X++)
				{
					Hist[X] = ColAvail[X + Y * GX] ? (Hist[X] + 1) : 0;
				}

				// Largest rectangle in histogram (stack-based, O(GX))
				Stack.Reset();

				for (int32 X = 0; X <= GX; X++)
				{
					const int32 H = (X < GX) ? Hist[X] : 0;
					int32 Start = X;

					while (Stack.Num() > 0 && Stack.Last().Value >= H)
					{
						const int32 StackIdx = Stack.Last().Key;
						const int32 StackHeight = Stack.Last().Value;
						Stack.Pop(EAllowShrinking::No);

						const int32 Width = X - StackIdx;
						const int32 Volume = Width * StackHeight * ZDepth;

						// Score: pure volume when Balance=0, cube-like preference when Balance>0
						double Score;
						if (bUseBalance)
						{
							// Compactness = second-largest / largest dimension (1.0 = perfect cube/square)
							int32 d1 = Width, d2 = StackHeight, d3 = ZDepth;
							if (d1 < d2) { Swap(d1, d2); }
							if (d1 < d3) { Swap(d1, d3); }
							if (d2 < d3) { Swap(d2, d3); }
							const double Compactness = static_cast<double>(d2) / d1;
							Score = Volume * FMath::Pow(Compactness, Balance * 2.0);
						}
						else
						{
							Score = static_cast<double>(Volume);
						}

						if (Score > BestScore)
						{
							BestScore = Score;
							OutVolume = Volume;
							OutMin = FIntVector(StackIdx, Y - StackHeight + 1, Z1);
							OutMax = FIntVector(X - 1, Y, Z2);
						}

						Start = StackIdx;
					}

					Stack.Add(TPair<int32, int32>(Start, H));
				}
			}
		}
	}

	return OutVolume > 0;
}

void FPCGExDecompMaxBoxes::MergeAdjacentCells(
	const FPCGExDecompOccupancyGrid& Grid,
	TArray<int32>& VoxelCellIDs,
	int32& NextCellID,
	const FIntVector& MaxExtent) const
{
	static constexpr int32 Dx[] = {1, -1, 0, 0, 0, 0};
	static constexpr int32 Dy[] = {0, 0, 1, -1, 0, 0};
	static constexpr int32 Dz[] = {0, 0, 0, 0, 1, -1};

	struct FCellInfo
	{
		FIntVector Min = FIntVector(MAX_int32, MAX_int32, MAX_int32);
		FIntVector Max = FIntVector(MIN_int32, MIN_int32, MIN_int32);
		int32 Count = 0;
	};

	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;

		// Build per-cell AABB and voxel count
		TMap<int32, FCellInfo> Cells;
		for (int32 Flat = 0; Flat < Grid.TotalVoxels; Flat++)
		{
			const int32 CellID = VoxelCellIDs[Flat];
			if (CellID < 0) { continue; }

			const FIntVector Coord = Grid.UnflatIndex(Flat);
			FCellInfo& Info = Cells.FindOrAdd(CellID);
			Info.Min = FIntVector(
				FMath::Min(Info.Min.X, Coord.X),
				FMath::Min(Info.Min.Y, Coord.Y),
				FMath::Min(Info.Min.Z, Coord.Z));
			Info.Max = FIntVector(
				FMath::Max(Info.Max.X, Coord.X),
				FMath::Max(Info.Max.Y, Coord.Y),
				FMath::Max(Info.Max.Z, Coord.Z));
			Info.Count++;
		}

		if (Cells.Num() <= 1) { break; }

		// Build face-adjacency between cells
		TMap<int32, TSet<int32>> Adj;
		for (int32 Flat = 0; Flat < Grid.TotalVoxels; Flat++)
		{
			const int32 CellID = VoxelCellIDs[Flat];
			if (CellID < 0) { continue; }

			const FIntVector Coord = Grid.UnflatIndex(Flat);
			for (int32 Dir = 0; Dir < 6; Dir++)
			{
				const int32 NX = Coord.X + Dx[Dir];
				const int32 NY = Coord.Y + Dy[Dir];
				const int32 NZ = Coord.Z + Dz[Dir];
				if (!Grid.IsInBounds(NX, NY, NZ)) { continue; }

				const int32 NCellID = VoxelCellIDs[Grid.FlatIndex(NX, NY, NZ)];
				if (NCellID >= 0 && NCellID != CellID)
				{
					Adj.FindOrAdd(CellID).Add(NCellID);
				}
			}
		}

		// Sort cells by count ascending (merge smallest cells first)
		TArray<int32> SortedCellIDs;
		Cells.GetKeys(SortedCellIDs);
		SortedCellIDs.Sort([&Cells](int32 A, int32 B) { return Cells[A].Count < Cells[B].Count; });

		for (const int32 CellA : SortedCellIDs)
		{
			const FCellInfo* InfoA = Cells.Find(CellA);
			if (!InfoA) { continue; }

			const TSet<int32>* AdjSet = Adj.Find(CellA);
			if (!AdjSet) { continue; }

			for (const int32 CellB : *AdjSet)
			{
				const FCellInfo* InfoB = Cells.Find(CellB);
				if (!InfoB) { continue; }

				// Compute merged AABB
				const FIntVector MMin(
					FMath::Min(InfoA->Min.X, InfoB->Min.X),
					FMath::Min(InfoA->Min.Y, InfoB->Min.Y),
					FMath::Min(InfoA->Min.Z, InfoB->Min.Z));
				const FIntVector MMax(
					FMath::Max(InfoA->Max.X, InfoB->Max.X),
					FMath::Max(InfoA->Max.Y, InfoB->Max.Y),
					FMath::Max(InfoA->Max.Z, InfoB->Max.Z));
				const FIntVector MSize = MMax - MMin + FIntVector(1, 1, 1);

				// Check MaxExtent
				if (MSize.X > MaxExtent.X || MSize.Y > MaxExtent.Y || MSize.Z > MaxExtent.Z) { continue; }

				// Perfect box check: merged AABB volume must equal combined voxel count
				const int32 MergedVolume = MSize.X * MSize.Y * MSize.Z;
				if (MergedVolume != InfoA->Count + InfoB->Count) { continue; }

				// Valid merge — absorb B into A
				for (int32 Flat = 0; Flat < Grid.TotalVoxels; Flat++)
				{
					if (VoxelCellIDs[Flat] == CellB) { VoxelCellIDs[Flat] = CellA; }
				}

				Cells.FindOrAdd(CellA) = FCellInfo{MMin, MMax, InfoA->Count + InfoB->Count};
				Cells.Remove(CellB);

				bChanged = true;
				break;
			}

			if (bChanged) { break; }
		}
	}

	// Re-compact CellIDs to be sequential after merges
	TMap<int32, int32> Remap;
	int32 CompactID = 0;
	for (int32 Flat = 0; Flat < Grid.TotalVoxels; Flat++)
	{
		if (VoxelCellIDs[Flat] < 0) { continue; }
		if (!Remap.Contains(VoxelCellIDs[Flat])) { Remap.Add(VoxelCellIDs[Flat], CompactID++); }
		VoxelCellIDs[Flat] = Remap[VoxelCellIDs[Flat]];
	}
	NextCellID = CompactID;
}

void FPCGExDecompMaxBoxes::SubdivideAndClaim(
	const FPCGExDecompOccupancyGrid& Grid,
	const FIntVector& BoxMin,
	const FIntVector& BoxMax,
	const FIntVector& MaxExtent,
	TBitArray<>& Available,
	TArray<int32>& VoxelCellIDs,
	int32& NextCellID,
	int32& RemainingCount,
	TArray<int32>& CellVoxelCounts) const
{
	const FIntVector BoxSize = BoxMax - BoxMin + FIntVector(1, 1, 1);

	// How many chunks per axis
	const FIntVector NumChunks = FIntVector(
		(BoxSize.X + MaxExtent.X - 1) / MaxExtent.X,
		(BoxSize.Y + MaxExtent.Y - 1) / MaxExtent.Y,
		(BoxSize.Z + MaxExtent.Z - 1) / MaxExtent.Z);

	// Even chunk size per axis (last chunk may be smaller due to clamping to BoxMax)
	const FIntVector ChunkSize = FIntVector(
		(BoxSize.X + NumChunks.X - 1) / NumChunks.X,
		(BoxSize.Y + NumChunks.Y - 1) / NumChunks.Y,
		(BoxSize.Z + NumChunks.Z - 1) / NumChunks.Z);

	for (int32 CZ = 0; CZ < NumChunks.Z; CZ++)
	{
		for (int32 CY = 0; CY < NumChunks.Y; CY++)
		{
			for (int32 CX = 0; CX < NumChunks.X; CX++)
			{
				const FIntVector ChunkMin = FIntVector(
					BoxMin.X + CX * ChunkSize.X,
					BoxMin.Y + CY * ChunkSize.Y,
					BoxMin.Z + CZ * ChunkSize.Z);

				const FIntVector ChunkMax = FIntVector(
					FMath::Min(ChunkMin.X + ChunkSize.X - 1, BoxMax.X),
					FMath::Min(ChunkMin.Y + ChunkSize.Y - 1, BoxMax.Y),
					FMath::Min(ChunkMin.Z + ChunkSize.Z - 1, BoxMax.Z));

				const int32 CellID = NextCellID++;
				int32 VoxelCount = 0;

				for (int32 Z = ChunkMin.Z; Z <= ChunkMax.Z; Z++)
				{
					for (int32 Y = ChunkMin.Y; Y <= ChunkMax.Y; Y++)
					{
						for (int32 X = ChunkMin.X; X <= ChunkMax.X; X++)
						{
							const int32 Flat = Grid.FlatIndex(X, Y, Z);
							VoxelCellIDs[Flat] = CellID;
							Available[Flat] = false;
							RemainingCount--;
							VoxelCount++;
						}
					}
				}

				CellVoxelCounts.Add(VoxelCount);
			}
		}
	}
}

#pragma endregion

#pragma region UPCGExDecompMaxBoxes

void UPCGExDecompMaxBoxes::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompMaxBoxes* TypedOther = Cast<UPCGExDecompMaxBoxes>(Other))
	{
		TransformSpace = TypedOther->TransformSpace;
		CustomTransform = TypedOther->CustomTransform;
		VoxelSizeMode = TypedOther->VoxelSizeMode;
		VoxelSize = TypedOther->VoxelSize;
		MaxCellSize = TypedOther->MaxCellSize;
		MinVoxelsPerCell = TypedOther->MinVoxelsPerCell;
		Balance = TypedOther->Balance;
	}
}

#pragma endregion
