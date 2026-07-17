// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeDBSCAN.h"
#include "PCGExVersion.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(DBSCAN, {}, {})

#if WITH_EDITOR
void UPCGExProbeDBSCANProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeDBSCANProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeDBSCAN::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeDBSCAN::WantsOctree() const
{
	return true;
}

bool FPCGExProbeDBSCAN::Prepare(FPCGExContext* InContext)
{
	return FPCGExProbeOperation::Prepare(InContext);
}

void FPCGExProbeDBSCAN::ProcessAll(TSet<uint64>& OutEdges) const
{
	const TArray<FVector>& Positions = *WorkingPositions;
	const int32 NumPoints = Positions.Num();
	if (NumPoints < 2)
	{
		return;
	}

	const TArray<int8>& CanGenerateRef = *CanGenerate;
	const TArray<int8>& AcceptConnectionsRef = *AcceptConnections;

	// First pass: identify core points and their neighbors
	TArray<TArray<int32>> Neighborhoods;
	TArray<bool> IsCore;
	Neighborhoods.SetNum(NumPoints);
	IsCore.Init(false, NumPoints);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i] && !AcceptConnectionsRef[i])
		{
			continue;
		}

		const FVector& Pos = Positions[i];
		const double MaxDistSq = GetSearchRadius(i);
		const double MaxDist = FMath::Sqrt(MaxDistSq);

		Octree->FindElementsWithBoundsTest(
			FBox(Pos - FVector(MaxDist), Pos + FVector(MaxDist)),
			[&](const PCGExOctree::FItem& Other)
			{
				const int32 j = Other.Index;
				if (i == j)
				{
					return;
				}
				if (!CanGenerateRef[j] && !AcceptConnectionsRef[j])
				{
					return;
				}

				if (FVector::DistSquared(Pos, Positions[j]) <= MaxDistSq)
				{
					Neighborhoods[i].Add(j);
				}
			});

		IsCore[i] = Neighborhoods[i].Num() >= Config.MinPoints;
	}

	// Second pass: create edges
	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		if (IsCore[i])
		{
			// Core point: connect to neighbors
			for (const int32 j : Neighborhoods[i])
			{
				if (Config.bCoreToCorOnly && !IsCore[j])
				{
					continue;
				}

				OutEdges.Add(PCGEx::H64U(i, j));
			}
		}
		else if (!Config.bCoreToCorOnly)
		{
			// Border point: connect to core point(s)
			if (Config.bBorderToNearestCoreOnly)
			{
				// Find nearest core point
				double BestDist = TNumericLimits<double>::Max();
				int32 BestCore = INDEX_NONE;

				for (const int32 j : Neighborhoods[i])
				{
					if (IsCore[j])
					{
						const double Dist = FVector::DistSquared(Positions[i], Positions[j]);
						if (Dist < BestDist)
						{
							BestDist = Dist;
							BestCore = j;
						}
					}
				}

				if (BestCore != INDEX_NONE)
				{
					OutEdges.Add(PCGEx::H64U(i, BestCore));
				}
			}
			else
			{
				// Connect to all reachable core points
				for (const int32 j : Neighborhoods[i])
				{
					if (IsCore[j])
					{
						OutEdges.Add(PCGEx::H64U(i, j));
					}
				}
			}
		}
	}
}
