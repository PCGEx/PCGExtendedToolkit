// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompBSPOccupancy.h"

#pragma region FPCGExDecompBSPOccupancy

bool FPCGExDecompBSPOccupancy::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() == 0) { return false; }

	// Auto-detect voxel size from cluster edge lengths
	const FVector VoxelSize = ComputeVoxelSize();

	// Build occupancy grid
	FPCGExDecompOccupancyGrid Grid;
	if (!Grid.Build(Cluster, TransformSpace, VoxelSize, CustomTransform)) { return false; }

	// Per-voxel CellID array (indexed by flat voxel index)
	TArray<int32> VoxelCellIDs;
	VoxelCellIDs.SetNumUninitialized(Grid.TotalVoxels);
	for (int32& ID : VoxelCellIDs) { ID = -1; }

	// Start recursive BSP over entire grid bounds
	FRegion FullRegion;
	FullRegion.Min = FIntVector::ZeroValue;
	FullRegion.Max = FIntVector(Grid.GridDimensions.X - 1, Grid.GridDimensions.Y - 1, Grid.GridDimensions.Z - 1);

	int32 NextCellID = 0;
	SplitRecursive(Grid, FullRegion, 0, VoxelCellIDs, NextCellID);

	// Post-process: split leaf cells where occupied voxels are non-contiguous
	SplitNonContiguousCells(Grid, VoxelCellIDs, NextCellID);

	// Map voxel CellIDs back to node CellIDs
	const int32 NumNodes = Cluster->Nodes->Num();
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

FVector FPCGExDecompBSPOccupancy::ComputeVoxelSize() const
{
	if (!Cluster || Cluster->Nodes->Num() < 2) { return FVector(100.0); }

	const int32 NumNodes = Cluster->Nodes->Num();
	double TotalDist = 0;
	int32 EdgeCount = 0;

	for (int32 i = 0; i < NumNodes; i++)
	{
		const PCGExClusters::FNode* Node = Cluster->GetNode(i);
		if (!Node->bValid) { continue; }

		const FVector NodePos = Cluster->GetPos(i);
		for (const PCGExGraphs::FLink& Lk : Node->Links)
		{
			TotalDist += FVector::Dist(NodePos, Cluster->GetPos(Lk.Node));
			EdgeCount++;
		}
	}

	if (EdgeCount == 0) { return FVector(100.0); }

	const double AvgEdgeLength = FMath::Max(TotalDist / EdgeCount, KINDA_SMALL_NUMBER);
	return FVector(AvgEdgeLength);
}

void FPCGExDecompBSPOccupancy::SplitRecursive(
	const FPCGExDecompOccupancyGrid& Grid,
	const FRegion& Region,
	const int32 Depth,
	TArray<int32>& VoxelCellIDs,
	int32& NextCellID) const
{
	const int32 OccupiedCount = CountOccupied(Grid, Region);

	// Base cases: make this a leaf cell
	if (OccupiedCount == 0) { return; }

	if (Depth >= MaxDepth || OccupiedCount <= MinVoxelsPerCell)
	{
		const int32 CellID = NextCellID++;
		for (int32 Z = Region.Min.Z; Z <= Region.Max.Z; Z++)
		{
			for (int32 Y = Region.Min.Y; Y <= Region.Max.Y; Y++)
			{
				for (int32 X = Region.Min.X; X <= Region.Max.X; X++)
				{
					if (Grid.IsOccupied(X, Y, Z))
					{
						VoxelCellIDs[Grid.FlatIndex(X, Y, Z)] = CellID;
					}
				}
			}
		}
		return;
	}

	// Try to find a good split
	int32 SplitAxis = -1;
	int32 SplitPos = -1;

	if (!FindBestSplit(Grid, Region, OccupiedCount, SplitAxis, SplitPos))
	{
		// No good split — make this a leaf
		const int32 CellID = NextCellID++;
		for (int32 Z = Region.Min.Z; Z <= Region.Max.Z; Z++)
		{
			for (int32 Y = Region.Min.Y; Y <= Region.Max.Y; Y++)
			{
				for (int32 X = Region.Min.X; X <= Region.Max.X; X++)
				{
					if (Grid.IsOccupied(X, Y, Z))
					{
						VoxelCellIDs[Grid.FlatIndex(X, Y, Z)] = CellID;
					}
				}
			}
		}
		return;
	}

	// Split into two sub-regions
	FRegion Left = Region;
	FRegion Right = Region;

	switch (SplitAxis)
	{
	case 0: // X
		Left.Max.X = SplitPos;
		Right.Min.X = SplitPos + 1;
		break;
	case 1: // Y
		Left.Max.Y = SplitPos;
		Right.Min.Y = SplitPos + 1;
		break;
	case 2: // Z
		Left.Max.Z = SplitPos;
		Right.Min.Z = SplitPos + 1;
		break;
	default: break;
	}

	SplitRecursive(Grid, Left, Depth + 1, VoxelCellIDs, NextCellID);
	SplitRecursive(Grid, Right, Depth + 1, VoxelCellIDs, NextCellID);
}

int32 FPCGExDecompBSPOccupancy::CountOccupied(
	const FPCGExDecompOccupancyGrid& Grid,
	const FRegion& Region) const
{
	int32 Count = 0;
	for (int32 Z = Region.Min.Z; Z <= Region.Max.Z; Z++)
	{
		for (int32 Y = Region.Min.Y; Y <= Region.Max.Y; Y++)
		{
			for (int32 X = Region.Min.X; X <= Region.Max.X; X++)
			{
				if (Grid.IsOccupied(X, Y, Z)) { Count++; }
			}
		}
	}
	return Count;
}

bool FPCGExDecompBSPOccupancy::FindBestSplit(
	const FPCGExDecompOccupancyGrid& Grid,
	const FRegion& Region,
	const int32 TotalOccupied,
	int32& OutAxis,
	int32& OutPosition) const
{
	double BestScore = -MAX_dbl;
	OutAxis = -1;
	OutPosition = -1;

	const int32 RegionSize[3] = {
		Region.Max.X - Region.Min.X + 1,
		Region.Max.Y - Region.Min.Y + 1,
		Region.Max.Z - Region.Min.Z + 1
	};

	for (int32 Axis = 0; Axis < 3; Axis++)
	{
		if (RegionSize[Axis] < 2) { continue; }

		// Compute per-slice occupancy counts along this axis
		const int32 SliceCount = RegionSize[Axis];
		TArray<int32> SliceOccupancy;
		SliceOccupancy.SetNumZeroed(SliceCount);

		for (int32 S = 0; S < SliceCount; S++)
		{
			const int32 SliceCoord = (Axis == 0 ? Region.Min.X : (Axis == 1 ? Region.Min.Y : Region.Min.Z)) + S;

			for (int32 A = (Axis == 0 ? Region.Min.Y : Region.Min.X); A <= (Axis == 0 ? Region.Max.Y : Region.Max.X); A++)
			{
				for (int32 B = (Axis <= 1 ? Region.Min.Z : Region.Min.Y); B <= (Axis <= 1 ? Region.Max.Z : Region.Max.Y); B++)
				{
					int32 X, Y, Z;
					switch (Axis)
					{
					case 0: X = SliceCoord; Y = A; Z = B; break;
					case 1: X = A; Y = SliceCoord; Z = B; break;
					default: X = A; Y = B; Z = SliceCoord; break;
					}

					if (Grid.IsOccupied(X, Y, Z)) { SliceOccupancy[S]++; }
				}
			}
		}

		// Build prefix sum for fast left/right counting
		TArray<int32> PrefixSum;
		PrefixSum.SetNumZeroed(SliceCount + 1);
		for (int32 S = 0; S < SliceCount; S++)
		{
			PrefixSum[S + 1] = PrefixSum[S] + SliceOccupancy[S];
		}

		// Evaluate each possible split position (between slice S and S+1)
		for (int32 S = 0; S < SliceCount - 1; S++)
		{
			const int32 LeftCount = PrefixSum[S + 1];
			const int32 RightCount = TotalOccupied - LeftCount;

			if (LeftCount == 0 || RightCount == 0) { continue; }

			const double Imbalance = FMath::Abs(static_cast<double>(LeftCount - RightCount)) / TotalOccupied;

			const int32 SliceArea = (Axis == 0 ? RegionSize[1] * RegionSize[2] : (Axis == 1 ? RegionSize[0] * RegionSize[2] : RegionSize[0] * RegionSize[1]));
			const double EmptyRatio = SliceArea > 0 ? 1.0 - (static_cast<double>(SliceOccupancy[S]) / SliceArea) : 0.0;

			const double Score = -Imbalance + GapWeight * EmptyRatio;

			if (Score > BestScore)
			{
				BestScore = Score;
				OutAxis = Axis;
				OutPosition = (Axis == 0 ? Region.Min.X : (Axis == 1 ? Region.Min.Y : Region.Min.Z)) + S;
			}
		}
	}

	return OutAxis >= 0;
}

void FPCGExDecompBSPOccupancy::SplitNonContiguousCells(
	const FPCGExDecompOccupancyGrid& Grid,
	TArray<int32>& VoxelCellIDs,
	int32& NextCellID) const
{
	static constexpr int32 Dx[] = {1, -1, 0, 0, 0, 0};
	static constexpr int32 Dy[] = {0, 0, 1, -1, 0, 0};
	static constexpr int32 Dz[] = {0, 0, 0, 0, 1, -1};

	TBitArray<> Visited;
	Visited.Init(false, Grid.TotalVoxels);

	TArray<int32> FinalCellIDs;
	FinalCellIDs.SetNumUninitialized(Grid.TotalVoxels);
	for (int32& ID : FinalCellIDs) { ID = -1; }

	int32 FinalNextCellID = 0;
	TArray<int32> Queue;

	for (int32 Flat = 0; Flat < Grid.TotalVoxels; Flat++)
	{
		if (VoxelCellIDs[Flat] < 0 || Visited[Flat]) { continue; }

		const int32 OrigCellID = VoxelCellIDs[Flat];
		const int32 NewCellID = FinalNextCellID++;

		Queue.Reset();
		Queue.Add(Flat);
		Visited[Flat] = true;

		while (Queue.Num() > 0)
		{
			const int32 Current = Queue.Pop(EAllowShrinking::No);
			FinalCellIDs[Current] = NewCellID;

			const FIntVector Coord = Grid.UnflatIndex(Current);

			for (int32 Dir = 0; Dir < 6; Dir++)
			{
				const int32 NX = Coord.X + Dx[Dir];
				const int32 NY = Coord.Y + Dy[Dir];
				const int32 NZ = Coord.Z + Dz[Dir];

				if (!Grid.IsInBounds(NX, NY, NZ)) { continue; }

				const int32 NFlat = Grid.FlatIndex(NX, NY, NZ);
				if (!Visited[NFlat] && VoxelCellIDs[NFlat] == OrigCellID)
				{
					Visited[NFlat] = true;
					Queue.Add(NFlat);
				}
			}
		}
	}

	VoxelCellIDs = MoveTemp(FinalCellIDs);
	NextCellID = FinalNextCellID;
}

#pragma endregion

#pragma region UPCGExDecompBSPOccupancy

void UPCGExDecompBSPOccupancy::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompBSPOccupancy* TypedOther = Cast<UPCGExDecompBSPOccupancy>(Other))
	{
		TransformSpace = TypedOther->TransformSpace;
		CustomTransform = TypedOther->CustomTransform;
		MaxCellSize = TypedOther->MaxCellSize;
		MaxDepth = TypedOther->MaxDepth;
		MinVoxelsPerCell = TypedOther->MinVoxelsPerCell;
		GapWeight = TypedOther->GapWeight;
	}
}

#pragma endregion
