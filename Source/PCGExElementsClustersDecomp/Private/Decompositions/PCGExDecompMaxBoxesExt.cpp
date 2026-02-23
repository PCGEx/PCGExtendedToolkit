// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompMaxBoxesExt.h"

#include "Data/PCGExData.h"
#include "Details/PCGExSettingsDetails.h"

#pragma region FPCGExDecompMaxBoxesExt

bool FPCGExDecompMaxBoxesExt::Decompose(FPCGExDecompositionResult& OutResult)
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

	// --- Resolve axis bias ---
	TSharedPtr<PCGExDetails::TSettingValue<FVector>> BiasSetting = AxisBias.GetValueSetting();
	BiasSetting->Init(PrimaryDataFacade);

	FVector ConstantBias = FVector(1.0);
	TArray<FVector> BiasPrefixSums;
	bool bUsePerNodeBias = false;

	if (BiasSetting->IsConstant())
	{
		ConstantBias = BiasSetting->Read(0);
		BiasSetting.Reset();
	}
	else
	{
		bUsePerNodeBias = true;
		TArray<FVector> VoxelBias;
		VoxelBias.SetNumUninitialized(Grid.TotalVoxels);
		for (int32 i = 0; i < Grid.TotalVoxels; i++) { VoxelBias[i] = FVector(1.0); }

		for (int32 i = 0; i < NumNodes; i++)
		{
			const int32 VoxelIdx = Grid.NodeToVoxelIndex[i];
			if (VoxelIdx >= 0) { VoxelBias[VoxelIdx] = BiasSetting->Read(Cluster->GetNodePointIndex(i)); }
		}

		BuildBiasPrefixSums(Grid, VoxelBias, BiasPrefixSums);
		BiasSetting.Reset();
	}

	// --- Resolve weight ---
	TSharedPtr<PCGExDetails::TSettingValue<double>> WeightSetting = Weight.GetValueSetting();
	WeightSetting->Init(PrimaryDataFacade);

	TArray<double> WeightPrefixSums;
	bool bUseWeights = false;

	if (WeightSetting->IsConstant())
	{
		// Constant weight = uniform = no effect on relative scoring
		WeightSetting.Reset();
	}
	else if (WeightInfluence > KINDA_SMALL_NUMBER)
	{
		bUseWeights = true;
		TArray<double> VoxelWeights;
		VoxelWeights.SetNumUninitialized(Grid.TotalVoxels);
		for (int32 i = 0; i < Grid.TotalVoxels; i++) { VoxelWeights[i] = 1.0; }

		for (int32 i = 0; i < NumNodes; i++)
		{
			const int32 VoxelIdx = Grid.NodeToVoxelIndex[i];
			if (VoxelIdx >= 0) { VoxelWeights[VoxelIdx] = WeightSetting->Read(Cluster->GetNodePointIndex(i)); }
		}

		BuildWeightPrefixSums(Grid, VoxelWeights, WeightPrefixSums);
		WeightSetting.Reset();
	}
	else
	{
		WeightSetting.Reset();
	}

	// --- Precompute heuristic edge scores ---
	TArray<double> EdgeScores;
	const bool bHasEdgeScores = bUseHeuristicMergeGating && Heuristics;

	if (bHasEdgeScores)
	{
		const int32 NumEdges = Cluster->Edges->Num();
		EdgeScores.SetNumZeroed(NumEdges);
		const PCGExClusters::FNode* Seed = Heuristics->GetRoamingSeed();
		const PCGExClusters::FNode* Goal = Heuristics->GetRoamingGoal();

		if (Seed && Goal)
		{
			for (int32 i = 0; i < NumEdges; i++)
			{
				const PCGExGraphs::FEdge& Edge = (*Cluster->Edges)[i];
				if (!Edge.bValid) { continue; }
				// Edge.Start/End are PointIndex — use GetEdgeStart/End to resolve to FNode*
				const PCGExClusters::FNode* StartNode = Cluster->GetEdgeStart(Edge);
				const PCGExClusters::FNode* EndNode = Cluster->GetEdgeEnd(Edge);
				EdgeScores[i] = Heuristics->GetEdgeScore(
					*StartNode, *EndNode, Edge, *Seed, *Goal);
			}
		}
	}

	// Available = occupied and not yet claimed
	TBitArray<> Available = Grid.Occupied;

	int32 RemainingCount = 0;
	for (int32 i = 0; i < Grid.TotalVoxels; i++) { if (Available[i]) { RemainingCount++; } }

	// Per-voxel CellID
	TArray<int32> VoxelCellIDs;
	VoxelCellIDs.SetNumUninitialized(Grid.TotalVoxels);
	for (int32& ID : VoxelCellIDs) { ID = -1; }

	int32 NextCellID = 0;
	TArray<int32> CellVoxelCounts;

	const TArray<double>* WeightPrefixSumsPtr = bUseWeights ? &WeightPrefixSums : nullptr;
	const TArray<FVector>* BiasPrefixSumsPtr = bUsePerNodeBias ? &BiasPrefixSums : nullptr;

	// --- Priority mode: two-pass extraction ---
	if (bUseWeights && WeightMode == EPCGExDecompWeightMode::Priority)
	{
		// Pass 1: only extract boxes whose average weight exceeds PriorityThreshold
		while (RemainingCount > 0)
		{
			FIntVector BoxMin, BoxMax;
			int32 BoxVolume = 0;

			if (!FindLargestBox(Grid, Available, WeightPrefixSumsPtr, ConstantBias, BiasPrefixSumsPtr, BoxMin, BoxMax, BoxVolume) || BoxVolume == 0) { break; }

			// Check average weight of the box
			const double WeightSum = QueryWeightSum(Grid, WeightPrefixSums, BoxMin, BoxMax);
			const double AvgWeight = WeightSum / BoxVolume;
			if (AvgWeight < PriorityThreshold) { break; } // No more high-priority boxes

			SubdivideAndClaim(Grid, BoxMin, BoxMax, MaxExtent, Available, VoxelCellIDs, NextCellID, RemainingCount, CellVoxelCounts);
		}
	}

	// Standard extraction (or Pass 2 for Priority mode)
	while (RemainingCount > 0)
	{
		FIntVector BoxMin, BoxMax;
		int32 BoxVolume = 0;

		if (!FindLargestBox(Grid, Available, WeightPrefixSumsPtr, ConstantBias, BiasPrefixSumsPtr, BoxMin, BoxMax, BoxVolume) || BoxVolume == 0) { break; }

		SubdivideAndClaim(Grid, BoxMin, BoxMax, MaxExtent, Available, VoxelCellIDs, NextCellID, RemainingCount, CellVoxelCounts);
	}

	// Merge adjacent cells that together form a perfect box
	MergeAdjacentCells(Grid, VoxelCellIDs, NextCellID, MaxExtent, bHasEdgeScores ? &EdgeScores : nullptr);

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

	// Map voxel CellIDs back to node CellIDs
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

bool FPCGExDecompMaxBoxesExt::FindLargestBox(
	const FPCGExDecompOccupancyGrid& Grid,
	const TBitArray<>& Available,
	const TArray<double>* WeightPrefixSums,
	const FVector& ConstantBias,
	const TArray<FVector>* BiasPrefixSums,
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
	const bool bHasPerNodeBias = BiasPrefixSums != nullptr;
	const bool bUseAxisBias = bHasPerNodeBias || !ConstantBias.Equals(FVector(1.0), KINDA_SMALL_NUMBER);
	const bool bUseWeightScoring = WeightPrefixSums != nullptr && WeightInfluence > KINDA_SMALL_NUMBER;
	const bool bUseVolumePreference = (PreferredMinVolume > KINDA_SMALL_NUMBER || PreferredMaxVolume > KINDA_SMALL_NUMBER) && VolumePreferenceWeight > KINDA_SMALL_NUMBER;

	// ColAvail[x + y * GX] = true iff ALL z-layers from Z1 to current Z2 at (x,y) are available
	TArray<bool> ColAvail;
	ColAvail.SetNum(GX * GY);

	// Y-direction histogram: Hist[x] = consecutive Y rows where ColAvail is true
	TArray<int32> Hist;
	Hist.SetNum(GX);

	// Stack for the largest-rectangle-in-histogram algorithm
	TArray<TPair<int32, int32>> Stack;

	for (int32 Z1 = 0; Z1 < GZ; Z1++)
	{
		// Reset ColAvail for new Z1
		for (int32 i = 0; i < GX * GY; i++) { ColAvail[i] = true; }

		for (int32 Z2 = Z1; Z2 < GZ; Z2++)
		{
			const int32 ZDepth = Z2 - Z1 + 1;

			// AND in the Z2 layer
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
			for (int32 X = 0; X < GX; X++) { Hist[X] = 0; }

			for (int32 Y = 0; Y < GY; Y++)
			{
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

						// --- Extended scoring ---
						double Score;
						if (bUseBalance || bUseAxisBias)
						{
							double d1, d2, d3;

							if (bUseAxisBias)
							{
								FVector EffectiveBias;
								if (bHasPerNodeBias)
								{
									const FIntVector CandMin(StackIdx, Y - StackHeight + 1, Z1);
									const FIntVector CandMax(X - 1, Y, Z2);
									const FVector BiasSum = QueryBiasSum(Grid, *BiasPrefixSums, CandMin, CandMax);
									EffectiveBias = BiasSum / static_cast<double>(Volume);
								}
								else
								{
									EffectiveBias = ConstantBias;
								}

								d1 = Width * EffectiveBias.X;
								d2 = StackHeight * EffectiveBias.Y;
								d3 = ZDepth * EffectiveBias.Z;
							}
							else
							{
								d1 = Width;
								d2 = StackHeight;
								d3 = ZDepth;
							}

							// Sort descending
							if (d1 < d2) { Swap(d1, d2); }
							if (d1 < d3) { Swap(d1, d3); }
							if (d2 < d3) { Swap(d2, d3); }

							const double Compactness = d1 > KINDA_SMALL_NUMBER ? d2 / d1 : 1.0;
							Score = Volume * FMath::Pow(Compactness, Balance * 2.0);
						}
						else
						{
							Score = static_cast<double>(Volume);
						}

						// Weight scoring
						if (bUseWeightScoring)
						{
							const FIntVector CandMin(StackIdx, Y - StackHeight + 1, Z1);
							const FIntVector CandMax(X - 1, Y, Z2);
							const double WeightSum = QueryWeightSum(Grid, *WeightPrefixSums, CandMin, CandMax);
							const double AvgWeight = WeightSum / Volume;
							Score *= FMath::Pow(FMath::Max(AvgWeight, KINDA_SMALL_NUMBER), WeightInfluence);
						}

						// Volume preference scoring
						if (bUseVolumePreference)
						{
							double VolumeFactor = 1.0;
							if (PreferredMinVolume > KINDA_SMALL_NUMBER && Volume < PreferredMinVolume)
							{
								VolumeFactor = static_cast<double>(Volume) / PreferredMinVolume;
							}
							if (PreferredMaxVolume > KINDA_SMALL_NUMBER && Volume > PreferredMaxVolume)
							{
								VolumeFactor = PreferredMaxVolume / static_cast<double>(Volume);
							}
							Score *= FMath::Pow(FMath::Max(VolumeFactor, KINDA_SMALL_NUMBER), VolumePreferenceWeight);
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

void FPCGExDecompMaxBoxesExt::MergeAdjacentCells(
	const FPCGExDecompOccupancyGrid& Grid,
	TArray<int32>& VoxelCellIDs,
	int32& NextCellID,
	const FIntVector& MaxExtent,
	const TArray<double>* EdgeScores) const
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

				// Heuristic merge gating: check boundary edge scores
				if (EdgeScores)
				{
					double ScoreSum = 0;
					int32 BoundaryEdgeCount = 0;
					const int32 NumEdgesTotal = Cluster->Edges->Num();

					for (int32 EdgeIdx = 0; EdgeIdx < NumEdgesTotal; EdgeIdx++)
					{
						const PCGExGraphs::FEdge& Edge = (*Cluster->Edges)[EdgeIdx];
						if (!Edge.bValid) { continue; }

						// Edge.Start/End are PointIndex — convert to NodeIndex via lookup
						const int32 NodeA = Cluster->NodeIndexLookup->Get(Edge.Start);
						const int32 NodeB = Cluster->NodeIndexLookup->Get(Edge.End);
						if (NodeA < 0 || NodeB < 0) { continue; }

						const int32 VoxA = Grid.NodeToVoxelIndex[NodeA];
						const int32 VoxB = Grid.NodeToVoxelIndex[NodeB];
						if (VoxA < 0 || VoxB < 0) { continue; }

						const int32 CA = VoxelCellIDs[VoxA];
						const int32 CB = VoxelCellIDs[VoxB];
						if ((CA == CellA && CB == CellB) || (CA == CellB && CB == CellA))
						{
							ScoreSum += (*EdgeScores)[EdgeIdx];
							BoundaryEdgeCount++;
						}
					}

					if (BoundaryEdgeCount > 0 && (ScoreSum / BoundaryEdgeCount) > MergeScoreThreshold)
					{
						continue; // Skip merge — boundary edges score too high
					}
				}

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

void FPCGExDecompMaxBoxesExt::SubdivideAndClaim(
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

	const FIntVector NumChunks = FIntVector(
		(BoxSize.X + MaxExtent.X - 1) / MaxExtent.X,
		(BoxSize.Y + MaxExtent.Y - 1) / MaxExtent.Y,
		(BoxSize.Z + MaxExtent.Z - 1) / MaxExtent.Z);

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

void FPCGExDecompMaxBoxesExt::BuildWeightPrefixSums(
	const FPCGExDecompOccupancyGrid& Grid,
	const TArray<double>& VoxelWeights,
	TArray<double>& OutPrefixSums) const
{
	const int32 GX = Grid.GridDimensions.X;
	const int32 GY = Grid.GridDimensions.Y;
	const int32 GZ = Grid.GridDimensions.Z;

	OutPrefixSums.SetNumZeroed(Grid.TotalVoxels);

	// Build 3D prefix sum: P[x][y][z] = sum of all weights in box (0,0,0) to (x,y,z)
	for (int32 Z = 0; Z < GZ; Z++)
	{
		for (int32 Y = 0; Y < GY; Y++)
		{
			for (int32 X = 0; X < GX; X++)
			{
				const int32 Flat = Grid.FlatIndex(X, Y, Z);
				double Val = VoxelWeights[Flat];

				if (X > 0) { Val += OutPrefixSums[Grid.FlatIndex(X - 1, Y, Z)]; }
				if (Y > 0) { Val += OutPrefixSums[Grid.FlatIndex(X, Y - 1, Z)]; }
				if (Z > 0) { Val += OutPrefixSums[Grid.FlatIndex(X, Y, Z - 1)]; }

				if (X > 0 && Y > 0) { Val -= OutPrefixSums[Grid.FlatIndex(X - 1, Y - 1, Z)]; }
				if (X > 0 && Z > 0) { Val -= OutPrefixSums[Grid.FlatIndex(X - 1, Y, Z - 1)]; }
				if (Y > 0 && Z > 0) { Val -= OutPrefixSums[Grid.FlatIndex(X, Y - 1, Z - 1)]; }

				if (X > 0 && Y > 0 && Z > 0) { Val += OutPrefixSums[Grid.FlatIndex(X - 1, Y - 1, Z - 1)]; }

				OutPrefixSums[Flat] = Val;
			}
		}
	}
}

double FPCGExDecompMaxBoxesExt::QueryWeightSum(
	const FPCGExDecompOccupancyGrid& Grid,
	const TArray<double>& PrefixSums,
	const FIntVector& BoxMin,
	const FIntVector& BoxMax) const
{
	// Inclusion-exclusion on 3D prefix sums
	// Sum = P[x2][y2][z2]
	//      - P[x1-1][y2][z2] - P[x2][y1-1][z2] - P[x2][y2][z1-1]
	//      + P[x1-1][y1-1][z2] + P[x1-1][y2][z1-1] + P[x2][y1-1][z1-1]
	//      - P[x1-1][y1-1][z1-1]

	auto SafeGet = [&](int32 X, int32 Y, int32 Z) -> double
	{
		if (X < 0 || Y < 0 || Z < 0) { return 0.0; }
		return PrefixSums[Grid.FlatIndex(X, Y, Z)];
	};

	const int32 X1 = BoxMin.X, Y1 = BoxMin.Y, Z1 = BoxMin.Z;
	const int32 X2 = BoxMax.X, Y2 = BoxMax.Y, Z2 = BoxMax.Z;

	return SafeGet(X2, Y2, Z2)
		- SafeGet(X1 - 1, Y2, Z2) - SafeGet(X2, Y1 - 1, Z2) - SafeGet(X2, Y2, Z1 - 1)
		+ SafeGet(X1 - 1, Y1 - 1, Z2) + SafeGet(X1 - 1, Y2, Z1 - 1) + SafeGet(X2, Y1 - 1, Z1 - 1)
		- SafeGet(X1 - 1, Y1 - 1, Z1 - 1);
}

void FPCGExDecompMaxBoxesExt::BuildBiasPrefixSums(
	const FPCGExDecompOccupancyGrid& Grid,
	const TArray<FVector>& VoxelBias,
	TArray<FVector>& OutPrefixSums) const
{
	const int32 GX = Grid.GridDimensions.X;
	const int32 GY = Grid.GridDimensions.Y;
	const int32 GZ = Grid.GridDimensions.Z;

	OutPrefixSums.SetNum(Grid.TotalVoxels);
	for (FVector& V : OutPrefixSums) { V = FVector::ZeroVector; }

	for (int32 Z = 0; Z < GZ; Z++)
	{
		for (int32 Y = 0; Y < GY; Y++)
		{
			for (int32 X = 0; X < GX; X++)
			{
				const int32 Flat = Grid.FlatIndex(X, Y, Z);
				FVector Val = VoxelBias[Flat];

				if (X > 0) { Val += OutPrefixSums[Grid.FlatIndex(X - 1, Y, Z)]; }
				if (Y > 0) { Val += OutPrefixSums[Grid.FlatIndex(X, Y - 1, Z)]; }
				if (Z > 0) { Val += OutPrefixSums[Grid.FlatIndex(X, Y, Z - 1)]; }

				if (X > 0 && Y > 0) { Val -= OutPrefixSums[Grid.FlatIndex(X - 1, Y - 1, Z)]; }
				if (X > 0 && Z > 0) { Val -= OutPrefixSums[Grid.FlatIndex(X - 1, Y, Z - 1)]; }
				if (Y > 0 && Z > 0) { Val -= OutPrefixSums[Grid.FlatIndex(X, Y - 1, Z - 1)]; }

				if (X > 0 && Y > 0 && Z > 0) { Val += OutPrefixSums[Grid.FlatIndex(X - 1, Y - 1, Z - 1)]; }

				OutPrefixSums[Flat] = Val;
			}
		}
	}
}

FVector FPCGExDecompMaxBoxesExt::QueryBiasSum(
	const FPCGExDecompOccupancyGrid& Grid,
	const TArray<FVector>& PrefixSums,
	const FIntVector& BoxMin,
	const FIntVector& BoxMax) const
{
	auto SafeGet = [&](int32 X, int32 Y, int32 Z) -> FVector
	{
		if (X < 0 || Y < 0 || Z < 0) { return FVector::ZeroVector; }
		return PrefixSums[Grid.FlatIndex(X, Y, Z)];
	};

	const int32 X1 = BoxMin.X, Y1 = BoxMin.Y, Z1 = BoxMin.Z;
	const int32 X2 = BoxMax.X, Y2 = BoxMax.Y, Z2 = BoxMax.Z;

	return SafeGet(X2, Y2, Z2)
		- SafeGet(X1 - 1, Y2, Z2) - SafeGet(X2, Y1 - 1, Z2) - SafeGet(X2, Y2, Z1 - 1)
		+ SafeGet(X1 - 1, Y1 - 1, Z2) + SafeGet(X1 - 1, Y2, Z1 - 1) + SafeGet(X2, Y1 - 1, Z1 - 1)
		- SafeGet(X1 - 1, Y1 - 1, Z1 - 1);
}

#pragma endregion

#pragma region UPCGExDecompMaxBoxesExt

void UPCGExDecompMaxBoxesExt::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader)
{
	AxisBias.RegisterBufferDependencies(InContext, FacadePreloader);
	Weight.RegisterBufferDependencies(InContext, FacadePreloader);
}

void UPCGExDecompMaxBoxesExt::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompMaxBoxesExt* TypedOther = Cast<UPCGExDecompMaxBoxesExt>(Other))
	{
		TransformSpace = TypedOther->TransformSpace;
		CustomTransform = TypedOther->CustomTransform;
		VoxelSizeMode = TypedOther->VoxelSizeMode;
		VoxelSize = TypedOther->VoxelSize;
		MaxCellSize = TypedOther->MaxCellSize;
		MinVoxelsPerCell = TypedOther->MinVoxelsPerCell;
		Balance = TypedOther->Balance;
		AxisBias = TypedOther->AxisBias;
		Weight = TypedOther->Weight;
		WeightInfluence = TypedOther->WeightInfluence;
		WeightMode = TypedOther->WeightMode;
		PriorityThreshold = TypedOther->PriorityThreshold;
		PreferredMinVolume = TypedOther->PreferredMinVolume;
		PreferredMaxVolume = TypedOther->PreferredMaxVolume;
		VolumePreferenceWeight = TypedOther->VolumePreferenceWeight;
		bUseHeuristicMergeGating = TypedOther->bUseHeuristicMergeGating;
		MergeScoreThreshold = TypedOther->MergeScoreThreshold;
	}
}

#pragma endregion
