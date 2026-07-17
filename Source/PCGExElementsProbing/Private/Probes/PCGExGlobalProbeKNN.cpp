// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeKNN.h"
#include "PCGExVersion.h"

#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"

PCGEX_CREATE_PROBE_FACTORY(KNN, {}, {})

#if WITH_EDITOR
void UPCGExProbeKNNProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeKNNProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

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

	K = Config.K.GetValueSetting();
	if (!K->Init(PrimaryDataFacade))
	{
		return false;
	}

	return true;
}

void FPCGExProbeKNN::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8> CanGenerateRef = *CanGenerate;
	const TArray<int8> AcceptConnectionsRef = *AcceptConnections;

	// TODO : Use octree for faster queries, this is crude

	// For each point, find K nearest neighbors
	TArray<TPair<float, int32>> Distances;
	Distances.SetNum(NumPoints);

	const bool bMutual = Config.Mode == EPCGExProbeKNNMode::Mutual;

	// Build neighbor sets for each point
	TArray<TSet<int32>> NeighborSets;
	if (bMutual)
	{
		NeighborSets.SetNum(NumPoints);
	}

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		const int32 ActualK = FMath::Min(K->Read(i), NumPoints - 1);

		// Compute distances to all other points
		for (int32 j = 0; j < NumPoints; ++j)
		{
			Distances[j] = {(AcceptConnectionsRef[j] && j != i) ? FVector::DistSquared(Positions[i], Positions[j]) : TNumericLimits<double>::Max(), j};
		}

		// Partial sort to get K smallest
		Algo::Sort(Distances, [](const auto& A, const auto& B)
		{
			return A.Key < B.Key;
		});

		if (bMutual)
		{
			for (int32 k = 0; k < ActualK; ++k)
			{
				NeighborSets[i].Add(Distances[k].Value);
			}
		}
		else
		{
			// Add edges to K nearest
			for (int32 k = 0; k < ActualK; ++k)
			{
				OutEdges.Add(PCGEx::H64U(i, Distances[k].Value));
			}
		}
	}

	if (bMutual)
	{
		// Only add edge if mutual
		for (int32 i = 0; i < NumPoints; ++i)
		{
			for (const int32 j : NeighborSets[i])
			{
				if (j > i && NeighborSets[j].Contains(i))
				{
					OutEdges.Add(PCGEx::H64U(i, j));
				}
			}
		}
	}
}
