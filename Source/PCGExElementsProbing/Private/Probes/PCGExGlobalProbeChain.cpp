// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeChain.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(Chain, {}, {})

bool FPCGExProbeChain::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeChain::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	if (Config.SortMode == EPCGExProbeChainSortMode::ByAttribute)
	{
		SortBuffer = PrimaryDataFacade->GetBroadcaster<double>(Config.SortAttribute, true, false);
		if (!SortBuffer)
		{
			return false;
		}
	}

	return true;
}

void FPCGExProbeChain::ComputeHilbertOrder(const TConstArrayView<int32> Points, TArray<int32>& OutOrder) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Points.Num();

	// Find bounds
	FBox Bounds(ForceInit);
	for (const int32 Index : Points)
	{
		Bounds += Positions[Index];
	}

	const FVector Size = Bounds.GetSize();
	const double MaxSize = FMath::Max3(Size.X, Size.Y, Size.Z);

	// Compute Hilbert index for each point (simplified 3D version)
	TArray<TPair<uint64, int32>> HilbertIndices;
	HilbertIndices.SetNum(NumPoints);

	constexpr int32 HilbertOrder = 16; // 16-bit precision per axis
	const double Scale = (1 << HilbertOrder) / FMath::Max(MaxSize, 1.0);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		const FVector Normalized = (Positions[Points[i]] - Bounds.Min) * Scale;
		const uint32 X = FMath::Clamp(static_cast<uint32>(Normalized.X), 0u, (1u << HilbertOrder) - 1);
		const uint32 Y = FMath::Clamp(static_cast<uint32>(Normalized.Y), 0u, (1u << HilbertOrder) - 1);
		const uint32 Z = FMath::Clamp(static_cast<uint32>(Normalized.Z), 0u, (1u << HilbertOrder) - 1);

		// Simplified 3D Hilbert - interleave bits (Morton code as approximation)
		uint64 Morton = 0;
		for (int32 Bit = 0; Bit < HilbertOrder; ++Bit)
		{
			Morton |= (static_cast<uint64>((X >> Bit) & 1) << (3 * Bit));
			Morton |= (static_cast<uint64>((Y >> Bit) & 1) << (3 * Bit + 1));
			Morton |= (static_cast<uint64>((Z >> Bit) & 1) << (3 * Bit + 2));
		}

		HilbertIndices[i] = {Morton, Points[i]};
	}

	Algo::Sort(HilbertIndices, [](const auto& A, const auto& B)
	{
		return A.Key < B.Key;
	});

	OutOrder.SetNum(NumPoints);
	for (int32 i = 0; i < NumPoints; ++i)
	{
		OutOrder[i] = HilbertIndices[i].Value;
	}
}

void FPCGExProbeChain::ComputeGreedyTSPOrder(const TConstArrayView<int32> Points, TArray<int32>& OutOrder) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Points.Num();

	TSet<int32> Remaining;
	for (const int32 Index : Points)
	{
		Remaining.Add(Index);
	}

	OutOrder.Reserve(NumPoints);

	// Start from first point
	int32 Current = Points[0];
	OutOrder.Add(Current);
	Remaining.Remove(Current);

	// Greedily pick nearest unvisited
	while (Remaining.Num() > 0)
	{
		double BestDist = TNumericLimits<double>::Max();
		int32 BestNext = INDEX_NONE;

		for (const int32 Candidate : Remaining)
		{
			const double Dist = FVector::DistSquared(Positions[Current], Positions[Candidate]);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestNext = Candidate;
			}
		}

		OutOrder.Add(BestNext);
		Remaining.Remove(BestNext);
		Current = BestNext;
	}
}

void FPCGExProbeChain::ProcessAll(TSet<uint64>& OutEdges) const
{
	ForEachPointDomain([&](const TConstArrayView<int32> Points) { ProcessDomain(Points, OutEdges); });
}

void FPCGExProbeChain::ProcessDomain(const TConstArrayView<int32> Points, TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Points.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	// Build sorted order
	TArray<int32> Order;

	switch (Config.SortMode)
	{
	case EPCGExProbeChainSortMode::ByAttribute:
	{
		TArray<TPair<double, int32>> SortPairs;
		SortPairs.SetNum(NumPoints);
		for (int32 i = 0; i < NumPoints; ++i)
		{
			SortPairs[i] = {SortBuffer->Read(Points[i]), Points[i]};
		}
		Algo::Sort(SortPairs, [](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});
		Order.SetNum(NumPoints);
		for (int32 i = 0; i < NumPoints; ++i)
		{
			Order[i] = SortPairs[i].Value;
		}
	}
	break;

	case EPCGExProbeChainSortMode::ByAxisProjection:
	{
		const FVector Axis = Config.ProjectionAxis.GetSafeNormal();
		TArray<TPair<double, int32>> SortPairs;
		SortPairs.SetNum(NumPoints);
		for (int32 i = 0; i < NumPoints; ++i)
		{
			SortPairs[i] = {FVector::DotProduct(Positions[Points[i]], Axis), Points[i]};
		}
		Algo::Sort(SortPairs, [](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});
		Order.SetNum(NumPoints);
		for (int32 i = 0; i < NumPoints; ++i)
		{
			Order[i] = SortPairs[i].Value;
		}
	}
	break;

	case EPCGExProbeChainSortMode::BySpatialCurve:
		ComputeGreedyTSPOrder(Points, Order);
		break;

	case EPCGExProbeChainSortMode::ByHilbertCurve:
		ComputeHilbertOrder(Points, Order);
		break;
	}

	// Filter to only valid points
	TArray<int32> ValidOrder;
	ValidOrder.Reserve(NumPoints);
	for (const int32 Idx : Order)
	{
		if (CanGenerateRef[Idx] || AcceptConnectionsRef[Idx])
		{
			ValidOrder.Add(Idx);
		}
	}

	for (int32 i = 0; i < ValidOrder.Num() - 1; ++i)
	{
		const int32 A = ValidOrder[i];
		const int32 B = ValidOrder[i + 1];

		if (!CanConnect(A, B))
		{
			continue;
		}

		if (FVector::DistSquared(Positions[A], Positions[B]) <= FMath::Max(GetSearchRadius(A), GetSearchRadius(B)))
		{
			OutEdges.Add(PCGEx::H64U(A, B));
		}
	}

	if (Config.bClosedLoop && ValidOrder.Num() > 2)
	{
		const int32 First = ValidOrder[0];
		const int32 Last = ValidOrder.Last();

		if (CanConnect(First, Last) &&
			FVector::DistSquared(Positions[First], Positions[Last]) <= FMath::Max(GetSearchRadius(First), GetSearchRadius(Last)))
		{
			OutEdges.Add(PCGEx::H64U(First, Last));
		}
	}
}
