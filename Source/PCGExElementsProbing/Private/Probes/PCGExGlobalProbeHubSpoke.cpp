// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeHubSpoke.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(HubSpoke, {}, {})

bool FPCGExProbeHubSpoke::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeHubSpoke::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	if (Config.HubSelectionMode == EPCGExHubSelectionMode::ByAttribute)
	{
		HubAttributeBuffer = PrimaryDataFacade->GetBroadcaster<double>(Config.HubAttribute, true, false);
		if (!HubAttributeBuffer)
		{
			return false;
		}
	}

	return true;
}

void FPCGExProbeHubSpoke::SelectHubsByDensity(const TConstArrayView<int32> Points, TArray<int32>& OutHubs) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Points.Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	// Compute local density (inverse of average distance to K nearest neighbors)
	constexpr int32 DensityK = 5;
	TArray<TPair<double, int32>> DensityScores;
	DensityScores.Reserve(NumPoints);

	TArray<double> Distances;
	Distances.SetNum(NumPoints);

	for (const int32 i : Points)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		for (int32 b = 0; b < NumPoints; ++b)
		{
			const int32 j = Points[b];
			Distances[b] = (j != i) ? FVector::DistSquared(Positions[i], Positions[j]) : TNumericLimits<double>::Max();
		}

		Algo::Sort(Distances);

		double AvgDist = 0;
		const int32 K = FMath::Min(DensityK, NumPoints - 1);
		for (int32 k = 0; k < K; ++k)
		{
			AvgDist += FMath::Sqrt(Distances[k]);
		}
		AvgDist /= K;

		DensityScores.Add({1.0 / FMath::Max(AvgDist, SMALL_NUMBER), i});
	}

	// Sort by density (highest first)
	Algo::Sort(DensityScores, [](const auto& A, const auto& B)
	{
		return A.Key > B.Key;
	});

	// Take top N as hubs
	const int32 NumHubs = FMath::Min(Config.NumHubs, DensityScores.Num());
	for (int32 i = 0; i < NumHubs; ++i)
	{
		OutHubs.Add(DensityScores[i].Value);
	}
}

void FPCGExProbeHubSpoke::SelectHubsByAttribute(const TConstArrayView<int32> Points, TArray<int32>& OutHubs) const
{
	const int32 NumPoints = Points.Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	TArray<TPair<double, int32>> Scores;
	Scores.Reserve(NumPoints);

	for (const int32 i : Points)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}
		Scores.Add({HubAttributeBuffer->Read(i), i});
	}

	Algo::Sort(Scores, [](const auto& A, const auto& B)
	{
		return A.Key > B.Key;
	});

	const int32 NumHubs = FMath::Min(Config.NumHubs, Scores.Num());
	for (int32 i = 0; i < NumHubs; ++i)
	{
		OutHubs.Add(Scores[i].Value);
	}
}

void FPCGExProbeHubSpoke::SelectHubsByCentrality(const TConstArrayView<int32> Points, TArray<int32>& OutHubs) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Points.Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	// Compute centrality: points closest to local centroid of neighborhood
	TArray<TPair<double, int32>> CentralityScores;
	CentralityScores.Reserve(NumPoints);

	for (const int32 i : Points)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		// Compute centroid of points within radius
		const double Radius = GetSearchRadius(i);
		FVector Centroid = FVector::ZeroVector;
		int32 Count = 0;

		for (const int32 j : Points)
		{
			if (FVector::DistSquared(Positions[i], Positions[j]) <= Radius)
			{
				Centroid += Positions[j];
				Count++;
			}
		}

		if (Count > 0)
		{
			Centroid /= Count;
			const double DistToCentroid = FVector::Dist(Positions[i], Centroid);
			CentralityScores.Add({DistToCentroid, i}); // Lower is more central
		}
	}

	Algo::Sort(CentralityScores, [](const auto& A, const auto& B)
	{
		return A.Key < B.Key;
	});

	const int32 NumHubs = FMath::Min(Config.NumHubs, CentralityScores.Num());
	for (int32 i = 0; i < NumHubs; ++i)
	{
		OutHubs.Add(CentralityScores[i].Value);
	}
}

void FPCGExProbeHubSpoke::SelectHubsByKMeans(const TConstArrayView<int32> Points, TArray<int32>& OutHubs) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	// Collect valid points
	TArray<int32> ValidIndices;
	for (const int32 i : Points)
	{
		if (CanGenerateRef[i])
		{
			ValidIndices.Add(i);
		}
	}

	const int32 K = FMath::Min(Config.NumHubs, ValidIndices.Num());
	if (K == 0)
	{
		return;
	}

	// Initialize centroids randomly
	TArray<FVector> Centroids;
	Centroids.SetNum(K);

	FRandomStream RNG(42); // Deterministic
	TArray<int32> ShuffledIndices = ValidIndices;
	for (int32 i = ShuffledIndices.Num() - 1; i > 0; --i)
	{
		const int32 j = RNG.RandRange(0, i);
		Swap(ShuffledIndices[i], ShuffledIndices[j]);
	}

	for (int32 c = 0; c < K; ++c)
	{
		Centroids[c] = Positions[ShuffledIndices[c]];
	}

	// K-means iterations, one assignment per valid point
	TArray<int32> Assignments;
	Assignments.SetNum(ValidIndices.Num());

	for (int32 Iter = 0; Iter < Config.KMeansIterations; ++Iter)
	{
		// Assignment step
		for (int32 v = 0; v < ValidIndices.Num(); ++v)
		{
			const int32 i = ValidIndices[v];

			double BestDist = TNumericLimits<double>::Max();
			int32 BestCluster = 0;

			for (int32 c = 0; c < K; ++c)
			{
				const double Dist = FVector::DistSquared(Positions[i], Centroids[c]);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestCluster = c;
				}
			}
			Assignments[v] = BestCluster;
		}

		// Update step
		TArray<FVector> NewCentroids;
		TArray<int32> ClusterCounts;
		NewCentroids.Init(FVector::ZeroVector, K);
		ClusterCounts.Init(0, K);

		for (int32 v = 0; v < ValidIndices.Num(); ++v)
		{
			NewCentroids[Assignments[v]] += Positions[ValidIndices[v]];
			ClusterCounts[Assignments[v]]++;
		}

		for (int32 c = 0; c < K; ++c)
		{
			if (ClusterCounts[c] > 0)
			{
				Centroids[c] = NewCentroids[c] / ClusterCounts[c];
			}
		}
	}

	// Find point closest to each centroid. Claimed points are excluded so two converged
	// centroids can't resolve to the same hub (a duplicate becomes a self-loop edge downstream).
	TSet<int32> Claimed;
	Claimed.Reserve(K);

	for (int32 c = 0; c < K; ++c)
	{
		double BestDist = TNumericLimits<double>::Max();
		int32 BestPoint = INDEX_NONE;

		for (const int32 i : ValidIndices)
		{
			if (Claimed.Contains(i))
			{
				continue;
			}

			const double Dist = FVector::DistSquared(Positions[i], Centroids[c]);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestPoint = i;
			}
		}

		if (BestPoint != INDEX_NONE)
		{
			Claimed.Add(BestPoint);
			OutHubs.Add(BestPoint);
		}
	}
}

void FPCGExProbeHubSpoke::ProcessAll(TSet<uint64>& OutEdges) const
{
	ForEachPointDomain([&](const TConstArrayView<int32> Points) { ProcessDomain(Points, OutEdges); });
}

void FPCGExProbeHubSpoke::ProcessDomain(const TConstArrayView<int32> Points, TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	if (Points.Num() < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	// Select hubs
	TArray<int32> Hubs;
	switch (Config.HubSelectionMode)
	{
	case EPCGExHubSelectionMode::ByDensity:
		SelectHubsByDensity(Points, Hubs);
		break;
	case EPCGExHubSelectionMode::ByAttribute:
		SelectHubsByAttribute(Points, Hubs);
		break;
	case EPCGExHubSelectionMode::ByCentrality:
		SelectHubsByCentrality(Points, Hubs);
		break;
	case EPCGExHubSelectionMode::KMeansCentroids:
		SelectHubsByKMeans(Points, Hubs);
		break;
	}

	if (Hubs.Num() == 0)
	{
		return;
	}

	TSet<int32> HubSet(Hubs);

	if (Config.bConnectHubs)
	{
		for (int32 i = 0; i < Hubs.Num(); ++i)
		{
			for (int32 j = i + 1; j < Hubs.Num(); ++j)
			{
				const double MaxDistSq = FMath::Max(GetSearchRadius(Hubs[i]), GetSearchRadius(Hubs[j]));
				if (CanConnect(Hubs[i], Hubs[j]) && FVector::DistSquared(Positions[Hubs[i]], Positions[Hubs[j]]) <= MaxDistSq)
				{
					OutEdges.Add(PCGEx::H64U(Hubs[i], Hubs[j]));
				}
			}
		}
	}

	// Connect spokes to hubs
	for (const int32 i : Points)
	{
		if (HubSet.Contains(i))
		{
			continue;
		}
		if (!CanGenerateRef[i] && !AcceptConnectionsRef[i])
		{
			continue;
		}

		const double MaxDistSq = GetSearchRadius(i);

		if (Config.bNearestHubOnly)
		{
			// Find nearest hub
			double BestDist = TNumericLimits<double>::Max();
			int32 BestHub = INDEX_NONE;

			for (const int32 Hub : Hubs)
			{
				const double Dist = FVector::DistSquared(Positions[i], Positions[Hub]);
				if (Dist < BestDist && Dist <= MaxDistSq)
				{
					BestDist = Dist;
					BestHub = Hub;
				}
			}

			if (BestHub != INDEX_NONE && CanConnect(i, BestHub))
			{
				OutEdges.Add(PCGEx::H64U(i, BestHub));
			}
		}
		else
		{
			// Connect to all hubs within radius
			for (const int32 Hub : Hubs)
			{
				if (FVector::DistSquared(Positions[i], Positions[Hub]) <= MaxDistSq)
				{
					if (CanConnect(i, Hub))
					{
						OutEdges.Add(PCGEx::H64U(i, Hub));
					}
				}
			}
		}
	}
}
