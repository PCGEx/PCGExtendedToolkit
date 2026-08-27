// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Decompositions/PCGExDecompSpectral.h"

#pragma region FPCGExDecompSpectral

bool FPCGExDecompSpectral::Decompose(FPCGExDecompositionResult& OutResult)
{
	if (!Cluster || Cluster->Nodes->Num() == 0)
	{
		return false;
	}

	const int32 NumNodes = Cluster->Nodes->Num();

	// Gather valid nodes
	TArray<int32> ValidNodes;
	ValidNodes.Reserve(NumNodes);
	for (int32 i = 0; i < NumNodes; i++)
	{
		if (Cluster->GetNode(i)->bValid)
		{
			ValidNodes.Add(i);
		}
	}

	if (ValidNodes.Num() < 2)
	{
		return false;
	}

	const int32 SafePartitions = FMath::Max(NumPartitions, 2);

	// Recursive spectral bisection
	TArray<TArray<int32>> Partitions;
	BisectRecursive(ValidNodes, SafePartitions, Partitions);

	// Exact mode: split the largest partition until the count is met, falling back to an arbitrary
	// even split when no spectral cut exists (those extra cuts carry no spectral meaning).
	if (PartitionMode == EPCGExDecompSpectralPartitionMode::Exact)
	{
		while (Partitions.Num() < SafePartitions)
		{
			int32 LargestIdx = -1;
			int32 LargestSize = 1;
			for (int32 p = 0; p < Partitions.Num(); p++)
			{
				if (Partitions[p].Num() > LargestSize)
				{
					LargestSize = Partitions[p].Num();
					LargestIdx = p;
				}
			}

			if (LargestIdx < 0)
			{
				break;
			} // Every partition is a single node -- can't reach the target

			TArray<int32> A, B;
			if (!BisectOnce(Partitions[LargestIdx], A, B))
			{
				// No spectral cut available -- split by arrival order purely to honor the count.
				const TArray<int32>& Src = Partitions[LargestIdx];
				const int32 Half = Src.Num() / 2;
				A.Reset();
				B.Reset();
				for (int32 i = 0; i < Src.Num(); i++)
				{
					if (i < Half) { A.Add(Src[i]); }
					else { B.Add(Src[i]); }
				}
			}

			Partitions[LargestIdx] = MoveTemp(A);
			Partitions.Add(MoveTemp(B));
		}
	}

	if (Partitions.Num() == 0)
	{
		return false;
	}

	OutResult.NumCells = Partitions.Num();
	for (int32 CellIdx = 0; CellIdx < Partitions.Num(); CellIdx++)
	{
		for (const int32 NodeIndex : Partitions[CellIdx])
		{
			OutResult.NodeCellIDs[NodeIndex] = CellIdx;
		}
	}

	return true;
}

bool FPCGExDecompSpectral::ComputeFiedlerVector(
	const TArray<int32>& SubsetNodeIndices,
	TArray<double>& OutFiedler) const
{
	const int32 N = SubsetNodeIndices.Num();
	if (N < 2)
	{
		return false;
	}

	// Build local index mapping: NodeIndex -> local index within subset
	TMap<int32, int32> NodeToLocal;
	NodeToLocal.Reserve(N);
	for (int32 i = 0; i < N; i++)
	{
		NodeToLocal.Add(SubsetNodeIndices[i], i);
	}

	// Build graph Laplacian L = D - A in sparse form
	// For shifted power iteration, we need (sigma*I - L) where sigma > lambda_max(L)
	// lambda_max(L) <= 2 * max_degree for unweighted, but we use edge weights from heuristics

	// Compute adjacency weights and degree
	TArray<double> Degree;
	Degree.SetNumZeroed(N);

	// Sparse adjacency: for each local node, list of (local neighbor, weight)
	TArray<TArray<TPair<int32, double>>> Adjacency;
	Adjacency.SetNum(N);

	for (int32 i = 0; i < N; i++)
	{
		const int32 NodeIndex = SubsetNodeIndices[i];
		const PCGExClusters::FNode* Node = Cluster->GetNode(NodeIndex);

		for (const PCGExGraphs::FLink Lk : Node->Links)
		{
			const int32* LocalNeighbor = NodeToLocal.Find(Lk.Node);
			if (!LocalNeighbor)
			{
				continue;
			} // Neighbor not in subset

			// Edge weight from heuristics; the fallback is a flat 1 (WantsHeuristics is unconditional)
			double Weight = 1.0;
			if (Heuristics)
			{
				const PCGExClusters::FNode* Neighbor = Cluster->GetNode(Lk.Node);
				const PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(Lk.Edge);
				// Average both directions for symmetric weight
				const double ScoreAB = Heuristics->GetEdgeScore(*Node, *Neighbor, Edge, *Node, *Neighbor);
				const double ScoreBA = Heuristics->GetEdgeScore(*Neighbor, *Node, Edge, *Neighbor, *Node);
				Weight = FMath::Max((ScoreAB + ScoreBA) * 0.5, KINDA_SMALL_NUMBER);
			}

			Adjacency[i].Add(TPair<int32, double>(*LocalNeighbor, Weight));
			Degree[i] += Weight;
		}
	}

	// Find sigma = max(Degree) * 2 + 1 (upper bound on lambda_max)
	double MaxDegree = 0;
	for (const double D : Degree)
	{
		MaxDegree = FMath::Max(MaxDegree, D);
	}
	const double Sigma = MaxDegree * 2.0 + 1.0;

	// Shifted power iteration to find largest eigenvector of M = sigma*I - L
	// which corresponds to smallest eigenvector of L (Fiedler)
	// But we need the 2nd smallest, so we first find the smallest (constant vector),
	// then deflate and find the next.

	// The smallest eigenvector of L is always the constant vector (1/sqrt(N), ..., 1/sqrt(N))
	// So we just need to find the largest eigenvector of M that is orthogonal to the constant vector.

	// Initialize random vector orthogonal to constant vector
	TArray<double> V;
	V.SetNum(N);

	// Initialize with alternating values to break symmetry
	FRandomStream RNG(42);
	for (int32 i = 0; i < N; i++)
	{
		V[i] = RNG.FRandRange(-1.0, 1.0);
	}

	// Remove constant component (project out the constant eigenvector)
	double Sum = 0;
	for (const double Val : V)
	{
		Sum += Val;
	}
	const double Mean = Sum / N;
	for (double& Val : V)
	{
		Val -= Mean;
	}

	// Normalize
	double Norm = 0;
	for (const double Val : V)
	{
		Norm += Val * Val;
	}
	Norm = FMath::Sqrt(Norm);
	if (Norm < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	for (double& Val : V)
	{
		Val /= Norm;
	}

	TArray<double> NewV;
	NewV.SetNum(N);

	for (int32 Iter = 0; Iter < MaxIterations; Iter++)
	{
		// Compute NewV = M * V = (sigma*I - L) * V = sigma*V - L*V
		// L*V[i] = Degree[i]*V[i] - sum_j(A[i][j]*V[j])
		for (int32 i = 0; i < N; i++)
		{
			double LV_i = Degree[i] * V[i];
			for (const auto& Adj : Adjacency[i])
			{
				LV_i -= Adj.Value * V[Adj.Key];
			}
			NewV[i] = Sigma * V[i] - LV_i;
		}

		// Project out constant component
		Sum = 0;
		for (const double Val : NewV)
		{
			Sum += Val;
		}
		const double NewMean = Sum / N;
		for (double& Val : NewV)
		{
			Val -= NewMean;
		}

		// Normalize
		Norm = 0;
		for (const double Val : NewV)
		{
			Norm += Val * Val;
		}
		Norm = FMath::Sqrt(Norm);
		if (Norm < KINDA_SMALL_NUMBER)
		{
			return false;
		}
		for (double& Val : NewV)
		{
			Val /= Norm;
		}

		// Check convergence
		double Diff = 0;
		for (int32 i = 0; i < N; i++)
		{
			const double D = NewV[i] - V[i];
			Diff += D * D;
		}

		V = NewV;

		if (Diff < ConvergenceTolerance * ConvergenceTolerance)
		{
			break;
		}
	}

	OutFiedler = MoveTemp(V);
	return true;
}

bool FPCGExDecompSpectral::BisectOnce(
	const TArray<int32>& NodeIndices,
	TArray<int32>& OutA,
	TArray<int32>& OutB) const
{
	OutA.Reset();
	OutB.Reset();

	const int32 N = NodeIndices.Num();
	if (N < 2)
	{
		return false;
	}

	TArray<double> Fiedler;
	if (!ComputeFiedlerVector(NodeIndices, Fiedler))
	{
		return false;
	}

	if (PartitionMode == EPCGExDecompSpectralPartitionMode::Natural)
	{
		// Natural: cut at the Fiedler sign -- the graph's intrinsic bottleneck.
		for (int32 i = 0; i < N; i++)
		{
			if (Fiedler[i] >= 0) { OutA.Add(NodeIndices[i]); }
			else { OutB.Add(NodeIndices[i]); }
		}
		return OutA.Num() > 0 && OutB.Num() > 0;
	}

	// Balanced / Exact: median cut by Fiedler rank. Always yields two non-empty halves for N >= 2,
	// avoiding the degenerate all-one-sign failure the plain sign cut can hit.
	TArray<int32> Order;
	Order.SetNumUninitialized(N);
	for (int32 i = 0; i < N; i++)
	{
		Order[i] = i;
	}
	// Tie-break by node index so equal-Fiedler nodes split deterministically across toolchains
	// (TArray::Sort is an unstable introsort).
	Order.Sort([&Fiedler](const int32 A, const int32 B)
	{
		return Fiedler[A] != Fiedler[B] ? Fiedler[A] < Fiedler[B] : A < B;
	});

	const int32 Half = N / 2;
	for (int32 r = 0; r < N; r++)
	{
		if (r < Half) { OutA.Add(NodeIndices[Order[r]]); }
		else { OutB.Add(NodeIndices[Order[r]]); }
	}

	return OutA.Num() > 0 && OutB.Num() > 0;
}

void FPCGExDecompSpectral::BisectRecursive(
	const TArray<int32>& NodeIndices,
	int32 TargetPartitions,
	TArray<TArray<int32>>& OutPartitions) const
{
	if (TargetPartitions <= 1 || NodeIndices.Num() < 2)
	{
		OutPartitions.Add(NodeIndices);
		return;
	}

	TArray<int32> A, B;
	if (!BisectOnce(NodeIndices, A, B))
	{
		// Can't bisect (degenerate spectrum, or an all-one-sign Fiedler vector in Natural mode).
		OutPartitions.Add(NodeIndices);
		return;
	}

	const int32 HalfTarget = FMath::Max(TargetPartitions / 2, 1);

	// Recurse the first half, then hand the second half whatever budget the first didn't use, so a
	// branch that bottoms out early doesn't silently cost the cluster partitions it asked for.
	const int32 Before = OutPartitions.Num();
	BisectRecursive(A, HalfTarget, OutPartitions);
	const int32 ProducedA = OutPartitions.Num() - Before;

	const int32 RemainingTarget = FMath::Max(TargetPartitions - ProducedA, 1);
	BisectRecursive(B, RemainingTarget, OutPartitions);
}

#pragma endregion

#pragma region UPCGExDecompSpectral

void UPCGExDecompSpectral::CopySettingsFrom(const UPCGExInstancedFactory* Other)
{
	Super::CopySettingsFrom(Other);
	if (const UPCGExDecompSpectral* TypedOther = Cast<UPCGExDecompSpectral>(Other))
	{
		NumPartitions = TypedOther->NumPartitions;
		MaxIterations = TypedOther->MaxIterations;
		ConvergenceTolerance = TypedOther->ConvergenceTolerance;
		PartitionMode = TypedOther->PartitionMode;
	}
}

#pragma endregion
