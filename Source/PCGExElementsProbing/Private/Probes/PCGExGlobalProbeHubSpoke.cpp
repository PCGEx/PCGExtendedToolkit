// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeHubSpoke.h"
#include "PCGExVersion.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(HubSpoke, {}, {})

#if WITH_EDITOR
void UPCGExProbeHubSpokeProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeHubSpokeProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

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

void FPCGExProbeHubSpoke::SelectHubsByDensity(TArray<int32>& OutHubs) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	// Compute local density (inverse of average distance to K nearest neighbors)
	constexpr int32 DensityK = 5;
	TArray<TPair<double, int32>> DensityScores;
	DensityScores.Reserve(NumPoints);

	TArray<double> Distances;
	Distances.SetNum(NumPoints);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		for (int32 j = 0; j < NumPoints; ++j)
		{
			Distances[j] = (j != i) ? FVector::DistSquared(Positions[i], Positions[j]) : TNumericLimits<double>::Max();
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

void FPCGExProbeHubSpoke::SelectHubsByAttribute(TArray<int32>& OutHubs) const
{
	const int32 NumPoints = WorkingPositions->Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	TArray<TPair<double, int32>> Scores;
	Scores.Reserve(NumPoints);

	for (int32 i = 0; i < NumPoints; ++i)
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

void FPCGExProbeHubSpoke::SelectHubsByCentrality(TArray<int32>& OutHubs) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	// Compute centrality: points closest to local centroid of neighborhood
	TArray<TPair<double, int32>> CentralityScores;
	CentralityScores.Reserve(NumPoints);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		// Compute centroid of points within radius
		const double Radius = GetSearchRadius(i);
		FVector Centroid = FVector::ZeroVector;
		int32 Count = 0;

		for (int32 j = 0; j < NumPoints; ++j)
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

void FPCGExProbeHubSpoke::SelectHubsByKMeans(TArray<int32>& OutHubs) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	const TArray<int8>& CanGenerateRef = *CanGenerate;

	// Collect valid points
	TArray<int32> ValidIndices;
	for (int32 i = 0; i < NumPoints; ++i)
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

	// K-means iterations
	TArray<int32> Assignments;
	Assignments.SetNum(NumPoints);

	for (int32 Iter = 0; Iter < Config.KMeansIterations; ++Iter)
	{
		// Assignment step
		for (int32 i = 0; i < NumPoints; ++i)
		{
			if (!CanGenerateRef[i])
			{
				continue;
			}

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
			Assignments[i] = BestCluster;
		}

		// Update step
		TArray<FVector> NewCentroids;
		TArray<int32> ClusterCounts;
		NewCentroids.Init(FVector::ZeroVector, K);
		ClusterCounts.Init(0, K);

		for (int32 i = 0; i < NumPoints; ++i)
		{
			if (!CanGenerateRef[i])
			{
				continue;
			}
			NewCentroids[Assignments[i]] += Positions[i];
			ClusterCounts[Assignments[i]]++;
		}

		for (int32 c = 0; c < K; ++c)
		{
			if (ClusterCounts[c] > 0)
			{
				Centroids[c] = NewCentroids[c] / ClusterCounts[c];
			}
		}
	}

	// Find point closest to each centroid
	for (int32 c = 0; c < K; ++c)
	{
		double BestDist = TNumericLimits<double>::Max();
		int32 BestPoint = INDEX_NONE;

		for (int32 i = 0; i < NumPoints; ++i)
		{
			if (!CanGenerateRef[i])
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
			OutHubs.Add(BestPoint);
		}
	}
}

void FPCGExProbeHubSpoke::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
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
		SelectHubsByDensity(Hubs);
		break;
	case EPCGExHubSelectionMode::ByAttribute:
		SelectHubsByAttribute(Hubs);
		break;
	case EPCGExHubSelectionMode::ByCentrality:
		SelectHubsByCentrality(Hubs);
		break;
	case EPCGExHubSelectionMode::KMeansCentroids:
		SelectHubsByKMeans(Hubs);
		break;
	}

	if (Hubs.Num() == 0)
	{
		return;
	}

	TSet<int32> HubSet(Hubs);

	// Connect hubs to each other
	if (Config.bConnectHubs)
	{
		for (int32 i = 0; i < Hubs.Num(); ++i)
		{
			for (int32 j = i + 1; j < Hubs.Num(); ++j)
			{
				const double MaxDistSq = FMath::Max(GetSearchRadius(Hubs[i]), GetSearchRadius(Hubs[j]));
				if (FVector::DistSquared(Positions[Hubs[i]], Positions[Hubs[j]]) <= MaxDistSq)
				{
					OutEdges.Add(PCGEx::H64U(Hubs[i], Hubs[j]));
				}
			}
		}
	}

	// Connect spokes to hubs
	for (int32 i = 0; i < NumPoints; ++i)
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

			if (BestHub != INDEX_NONE && (CanGenerateRef[i] || CanGenerateRef[BestHub]))
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
					if (CanGenerateRef[i] || CanGenerateRef[Hub])
					{
						OutEdges.Add(PCGEx::H64U(i, Hub));
					}
				}
			}
		}
	}
}
