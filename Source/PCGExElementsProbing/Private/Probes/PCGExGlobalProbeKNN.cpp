// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeKNN.h"

#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"

PCGEX_CREATE_PROBE_FACTORY(KNN, {}, {})

bool FPCGExProbeKNN::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeKNN::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	if (!InitSettingValue(K, Config.K.GetValueSetting(), PrimaryDataFacade))
	{
		return false;
	}

	return true;
}

void FPCGExProbeKNN::ProcessAll(TSet<uint64>& OutEdges) const
{
	ForEachPointDomain([&](const TConstArrayView<int32> Points) { ProcessDomain(Points, OutEdges); });
}

void FPCGExProbeKNN::ProcessDomain(const TConstArrayView<int32> Points, TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Points.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	// TODO : Use octree for faster queries, this is crude

	// For each point, find K nearest neighbors. Values and neighbor sets are domain-local indices.
	TArray<TPair<double, int32>> Distances;
	Distances.SetNum(NumPoints);

	const bool bMutual = Config.Mode == EPCGExProbeKNNMode::Mutual;

	// Build neighbor sets for each point
	TArray<TSet<int32>> NeighborSets;
	if (bMutual)
	{
		NeighborSets.SetNum(NumPoints);
	}

	for (int32 a = 0; a < NumPoints; ++a)
	{
		const int32 i = Points[a];
		if (!CanGenerateRef[i])
		{
			continue;
		}

		// Compute distances to all other points; sentinel-keyed entries (self, non-connectable)
		// must never be picked, so K is capped by the connectable count, not the dataset size.
		int32 NumConnectable = 0;
		for (int32 b = 0; b < NumPoints; ++b)
		{
			const int32 j = Points[b];
			const bool bConnectable = AcceptConnectionsRef[j] && j != i;
			NumConnectable += bConnectable ? 1 : 0;
			Distances[b] = {bConnectable ? FVector::DistSquared(Positions[i], Positions[j]) : TNumericLimits<double>::Max(), b};
		}

		const int32 ActualK = FMath::Min(K->Read(i), NumConnectable);

		// The lower index wins a tie (domains are ascending), so equidistant picks never depend on the sort.
		Algo::Sort(Distances, [](const TPair<double, int32>& A, const TPair<double, int32>& B)
		{
			return A.Key < B.Key || (A.Key == B.Key && A.Value < B.Value);
		});

		if (bMutual)
		{
			for (int32 k = 0; k < ActualK; ++k)
			{
				NeighborSets[a].Add(Distances[k].Value);
			}
		}
		else
		{
			// Add edges to K nearest
			for (int32 k = 0; k < ActualK; ++k)
			{
				OutEdges.Add(PCGEx::H64U(i, Points[Distances[k].Value]));
			}
		}
	}

	if (bMutual)
	{
		// Only add edge if mutual
		for (int32 a = 0; a < NumPoints; ++a)
		{
			for (const int32 b : NeighborSets[a])
			{
				if (b > a && NeighborSets[b].Contains(a))
				{
					OutEdges.Add(PCGEx::H64U(Points[a], Points[b]));
				}
			}
		}
	}
}
