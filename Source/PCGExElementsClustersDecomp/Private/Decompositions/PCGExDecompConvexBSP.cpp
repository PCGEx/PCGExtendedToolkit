// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompConvexBSP.h"

namespace PCGExDecompConvexBSPInternal
{
	struct FConvexCell3D
	{
		TArray<int32> NodeIndices;
	};

	static void ComputeConvexHull(
		const TArray<FVector>& Points,
		TArray<int32>& OutHullIndices)
	{
		OutHullIndices.Empty();

		const int32 NumPoints = Points.Num();
		if (NumPoints < 4)
		{
			for (int32 i = 0; i < NumPoints; i++) { OutHullIndices.Add(i); }
			return;
		}

		int32 MinX = 0, MaxX = 0;
		for (int32 i = 1; i < NumPoints; i++)
		{
			if (Points[i].X < Points[MinX].X) { MinX = i; }
			if (Points[i].X > Points[MaxX].X) { MaxX = i; }
		}

		if (MinX == MaxX)
		{
			for (int32 i = 0; i < NumPoints; i++) { OutHullIndices.Add(i); }
			return;
		}

		const FVector LineDir = (Points[MaxX] - Points[MinX]).GetSafeNormal();
		double MaxLineDist = 0;
		int32 ThirdPoint = -1;

		for (int32 i = 0; i < NumPoints; i++)
		{
			if (i == MinX || i == MaxX) { continue; }

			const FVector ToPoint = Points[i] - Points[MinX];
			const FVector Projected = Points[MinX] + LineDir * FVector::DotProduct(ToPoint, LineDir);
			const double DistSq = FVector::DistSquared(Points[i], Projected);

			if (DistSq > MaxLineDist)
			{
				MaxLineDist = DistSq;
				ThirdPoint = i;
			}
		}

		if (ThirdPoint < 0)
		{
			OutHullIndices.Add(MinX);
			OutHullIndices.Add(MaxX);
			return;
		}

		const FVector PlaneNormal = FVector::CrossProduct(
			Points[MaxX] - Points[MinX],
			Points[ThirdPoint] - Points[MinX]).GetSafeNormal();

		double MaxPlaneDist = 0;
		int32 FourthPoint = -1;

		for (int32 i = 0; i < NumPoints; i++)
		{
			if (i == MinX || i == MaxX || i == ThirdPoint) { continue; }

			const double Dist = FMath::Abs(FVector::DotProduct(Points[i] - Points[MinX], PlaneNormal));
			if (Dist > MaxPlaneDist)
			{
				MaxPlaneDist = Dist;
				FourthPoint = i;
			}
		}

		if (FourthPoint < 0 || MaxPlaneDist < KINDA_SMALL_NUMBER)
		{
			OutHullIndices.Add(MinX);
			OutHullIndices.Add(MaxX);
			OutHullIndices.Add(ThirdPoint);
			return;
		}

		TSet<int32> HullSet;
		HullSet.Add(MinX);
		HullSet.Add(MaxX);
		HullSet.Add(ThirdPoint);
		HullSet.Add(FourthPoint);

		struct FFace
		{
			int32 A, B, C;
			FVector Normal;
			double D;

			void ComputePlane(const TArray<FVector>& Pts)
			{
				Normal = FVector::CrossProduct(Pts[B] - Pts[A], Pts[C] - Pts[A]).GetSafeNormal();
				D = -FVector::DotProduct(Normal, Pts[A]);
			}

			double SignedDist(const FVector& P) const
			{
				return FVector::DotProduct(Normal, P) + D;
			}
		};

		TArray<FFace> Faces;

		auto AddFace = [&](int32 A, int32 B, int32 C)
		{
			FFace F;
			F.A = A;
			F.B = B;
			F.C = C;
			F.ComputePlane(Points);

			const FVector Centroid = (Points[MinX] + Points[MaxX] + Points[ThirdPoint] + Points[FourthPoint]) / 4.0;
			if (F.SignedDist(Centroid) > 0)
			{
				F.Normal = -F.Normal;
				F.D = -F.D;
				Swap(F.B, F.C);
			}

			Faces.Add(F);
		};

		AddFace(MinX, MaxX, ThirdPoint);
		AddFace(MinX, ThirdPoint, FourthPoint);
		AddFace(MinX, FourthPoint, MaxX);
		AddFace(MaxX, FourthPoint, ThirdPoint);

		for (int32 i = 0; i < NumPoints; i++)
		{
			if (HullSet.Contains(i)) { continue; }

			bool bOutside = false;
			for (const FFace& Face : Faces)
			{
				if (Face.SignedDist(Points[i]) > KINDA_SMALL_NUMBER)
				{
					bOutside = true;
					break;
				}
			}

			if (bOutside) { HullSet.Add(i); }
		}

		OutHullIndices = HullSet.Array();
	}

	static double ComputeConvexityRatio(const TArray<FVector>& Positions)
	{
		if (Positions.Num() <= 4) { return 0.0; }

		TArray<int32> HullIndices;
		ComputeConvexHull(Positions, HullIndices);

		if (HullIndices.Num() == 0) { return 1.0; }

		const int32 InteriorCount = Positions.Num() - HullIndices.Num();
		return static_cast<double>(InteriorCount) / Positions.Num();
	}

	static bool FindSplitPlane(
		const TArray<FVector>& Positions,
		FVector& OutPlaneOrigin,
		FVector& OutPlaneNormal)
	{
		if (Positions.Num() < 2) { return false; }

		FVector Centroid = FVector::ZeroVector;
		for (const FVector& P : Positions) { Centroid += P; }
		Centroid /= Positions.Num();

		double Cov[3][3] = {{0}};
		for (const FVector& P : Positions)
		{
			const FVector D = P - Centroid;
			Cov[0][0] += D.X * D.X;
			Cov[0][1] += D.X * D.Y;
			Cov[0][2] += D.X * D.Z;
			Cov[1][1] += D.Y * D.Y;
			Cov[1][2] += D.Y * D.Z;
			Cov[2][2] += D.Z * D.Z;
		}
		Cov[1][0] = Cov[0][1];
		Cov[2][0] = Cov[0][2];
		Cov[2][1] = Cov[1][2];

		FVector Axis = FVector(1, 0, 0);
		for (int32 Iter = 0; Iter < 50; Iter++)
		{
			FVector NewAxis;
			NewAxis.X = Cov[0][0] * Axis.X + Cov[0][1] * Axis.Y + Cov[0][2] * Axis.Z;
			NewAxis.Y = Cov[1][0] * Axis.X + Cov[1][1] * Axis.Y + Cov[1][2] * Axis.Z;
			NewAxis.Z = Cov[2][0] * Axis.X + Cov[2][1] * Axis.Y + Cov[2][2] * Axis.Z;

			const double Len = NewAxis.Size();
			if (Len > KINDA_SMALL_NUMBER) { Axis = NewAxis / Len; }
		}

		OutPlaneOrigin = Centroid;
		OutPlaneNormal = Axis.GetSafeNormal();

		if (OutPlaneNormal.IsNearlyZero()) { OutPlaneNormal = FVector::UpVector; }

		return true;
	}

	static void DecomposeRecursive(
		const PCGExClusters::FCluster* InCluster,
		const TArray<int32>& NodeIndices,
		const int32 MinNodesPerCell,
		const int32 MaxCells,
		const int32 InMaxDepth,
		const double InMaxConcavityRatio,
		TArray<FConvexCell3D>& OutCells,
		int32 Depth)
	{
		TArray<FVector> Positions;
		Positions.SetNum(NodeIndices.Num());
		for (int32 i = 0; i < NodeIndices.Num(); i++)
		{
			Positions[i] = InCluster->GetPos(NodeIndices[i]);
		}

		bool bShouldTerminate = false;

		if (Depth >= InMaxDepth) { bShouldTerminate = true; }
		else if (OutCells.Num() >= MaxCells) { bShouldTerminate = true; }
		else if (NodeIndices.Num() <= MinNodesPerCell) { bShouldTerminate = true; }
		else
		{
			const double ConvexityRatio = ComputeConvexityRatio(Positions);
			if (ConvexityRatio <= InMaxConcavityRatio) { bShouldTerminate = true; }
		}

		if (bShouldTerminate)
		{
			FConvexCell3D Cell;
			Cell.NodeIndices = NodeIndices;
			OutCells.Add(MoveTemp(Cell));
			return;
		}

		FVector PlaneOrigin, PlaneNormal;
		if (!FindSplitPlane(Positions, PlaneOrigin, PlaneNormal))
		{
			FConvexCell3D Cell;
			Cell.NodeIndices = NodeIndices;
			OutCells.Add(MoveTemp(Cell));
			return;
		}

		TArray<int32> FrontNodes, BackNodes;

		for (int32 i = 0; i < NodeIndices.Num(); i++)
		{
			const double Dist = FVector::DotProduct(Positions[i] - PlaneOrigin, PlaneNormal);
			if (Dist >= 0) { FrontNodes.Add(NodeIndices[i]); }
			else { BackNodes.Add(NodeIndices[i]); }
		}

		if (FrontNodes.Num() < MinNodesPerCell || BackNodes.Num() < MinNodesPerCell)
		{
			const FVector AltNormals[] = {
				FVector::CrossProduct(PlaneNormal, FVector::UpVector).GetSafeNormal(),
				FVector::CrossProduct(PlaneNormal, FVector::RightVector).GetSafeNormal(),
				FVector::CrossProduct(PlaneNormal, FVector::ForwardVector).GetSafeNormal()
			};

			bool bFoundValidSplit = false;
			for (const FVector& AltNormal : AltNormals)
			{
				if (AltNormal.IsNearlyZero()) { continue; }

				FrontNodes.Empty();
				BackNodes.Empty();

				for (int32 i = 0; i < NodeIndices.Num(); i++)
				{
					const double Dist = FVector::DotProduct(Positions[i] - PlaneOrigin, AltNormal);
					if (Dist >= 0) { FrontNodes.Add(NodeIndices[i]); }
					else { BackNodes.Add(NodeIndices[i]); }
				}

				if (FrontNodes.Num() >= MinNodesPerCell && BackNodes.Num() >= MinNodesPerCell)
				{
					bFoundValidSplit = true;
					break;
				}
			}

			if (!bFoundValidSplit)
			{
				FConvexCell3D Cell;
				Cell.NodeIndices = NodeIndices;
				OutCells.Add(MoveTemp(Cell));
				return;
			}
		}

		DecomposeRecursive(InCluster, FrontNodes, MinNodesPerCell, MaxCells, InMaxDepth, InMaxConcavityRatio, OutCells, Depth + 1);
		DecomposeRecursive(InCluster, BackNodes, MinNodesPerCell, MaxCells, InMaxDepth, InMaxConcavityRatio, OutCells, Depth + 1);
	}
}

#pragma region FPCGExDecompConvexBSP

bool FPCGExDecompConvexBSP::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() < 4) { return false; }

	TArray<int32> AllNodes;
	AllNodes.Reserve(Cluster->Nodes->Num());
	for (int32 i = 0; i < Cluster->Nodes->Num(); i++)
	{
		if (Cluster->GetNode(i)->bValid) { AllNodes.Add(i); }
	}

	if (AllNodes.Num() < MinNodesPerCell) { return false; }

	TArray<PCGExDecompConvexBSPInternal::FConvexCell3D> Cells;
	PCGExDecompConvexBSPInternal::DecomposeRecursive(
		Cluster.Get(), AllNodes, MinNodesPerCell, MaxCells, MaxDepth, MaxConcavityRatio, Cells, 0);

	if (Cells.Num() == 0) { return false; }

	OutResult.NumCells = Cells.Num();
	for (int32 CellIdx = 0; CellIdx < Cells.Num(); CellIdx++)
	{
		for (const int32 NodeIndex : Cells[CellIdx].NodeIndices)
		{
			OutResult.NodeCellIDs[NodeIndex] = CellIdx;
		}
	}

	return true;
}

#pragma endregion

#pragma region UPCGExDecompConvexBSP

void UPCGExDecompConvexBSP::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompConvexBSP* TypedOther = Cast<UPCGExDecompConvexBSP>(Other))
	{
		MaxConcavityRatio = TypedOther->MaxConcavityRatio;
		MinNodesPerCell = TypedOther->MinNodesPerCell;
		MaxCells = TypedOther->MaxCells;
		MaxDepth = TypedOther->MaxDepth;
	}
}

#pragma endregion
