// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeSpanner.h"
#include "PCGExVersion.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(Spanner, {}, {})

#if WITH_EDITOR
void UPCGExProbeSpannerProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeSpannerProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeSpanner::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeSpanner::Prepare(FPCGExContext* InContext)
{
	return FPCGExProbeOperation::Prepare(InContext);
}

double FPCGExProbeSpanner::GetGraphDistance(int32 From, int32 To,
                                            const TArray<TSet<int32>>& Adjacency, const TArray<FVector>& Positions) const
{
	if (From == To)
	{
		return 0.0;
	}

	const int32 NumPoints = Positions.Num();
	TArray<double> Dist;
	Dist.Init(TNumericLimits<double>::Max(), NumPoints);
	Dist[From] = 0.0;

	// Simple Dijkstra with priority queue
	TArray<TPair<double, int32>> PQ;
	PQ.HeapPush({0.0, From}, [](const auto& A, const auto& B)
	{
		return A.Key < B.Key;
	});

	while (PQ.Num() > 0)
	{
		TPair<double, int32> Current;
		PQ.HeapPop(Current, [](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});

		if (Current.Value == To)
		{
			return Current.Key;
		}
		if (Current.Key > Dist[Current.Value])
		{
			continue;
		}

		for (const int32 Neighbor : Adjacency[Current.Value])
		{
			const double EdgeDist = FVector::Dist(Positions[Current.Value], Positions[Neighbor]);
			const double NewDist = Dist[Current.Value] + EdgeDist;

			if (NewDist < Dist[Neighbor])
			{
				Dist[Neighbor] = NewDist;
				PQ.HeapPush({NewDist, Neighbor}, [](const auto& A, const auto& B)
				{
					return A.Key < B.Key;
				});
			}
		}
	}

	return TNumericLimits<double>::Max(); // Not reachable
}

void FPCGExProbeSpanner::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	// Build sorted list of all candidate edges
	struct FEdgeCandidate
	{
		int32 A, B;
		double Dist;
	};

	TArray<FEdgeCandidate> Candidates;
	Candidates.Reserve(FMath::Min(Config.MaxEdgeCandidates, NumPoints * (NumPoints - 1) / 2));

	for (int32 i = 0; i < NumPoints && Candidates.Num() < Config.MaxEdgeCandidates; ++i)
	{
		if (!CanGenerateRef[i] && !AcceptConnectionsRef[i])
		{
			continue;
		}

		for (int32 j = i + 1; j < NumPoints && Candidates.Num() < Config.MaxEdgeCandidates; ++j)
		{
			if (!CanGenerateRef[j] && !AcceptConnectionsRef[j])
			{
				continue;
			}
			if (!CanGenerateRef[i] && !CanGenerateRef[j])
			{
				continue;
			}

			Candidates.Add({i, j, FVector::Dist(Positions[i], Positions[j])});
		}
	}

	// Sort by distance (greedy processes shortest first)
	Algo::Sort(Candidates, [](const FEdgeCandidate& A, const FEdgeCandidate& B)
	{
		return A.Dist < B.Dist;
	});

	// Build adjacency list for path queries
	TArray<TSet<int32>> Adjacency;
	Adjacency.SetNum(NumPoints);

	// Greedy spanner construction
	for (const FEdgeCandidate& Edge : Candidates)
	{
		// Check if current graph distance exceeds t * Euclidean distance
		const double GraphDist = GetGraphDistance(Edge.A, Edge.B, Adjacency, Positions);

		if (GraphDist > Config.StretchFactor * Edge.Dist)
		{
			// Add edge
			OutEdges.Add(PCGEx::H64U(Edge.A, Edge.B));
			Adjacency[Edge.A].Add(Edge.B);
			Adjacency[Edge.B].Add(Edge.A);
		}
	}
}
