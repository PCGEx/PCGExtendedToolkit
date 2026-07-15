// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Probes/PCGExGlobalProbeGradientFlow.h"
#include "PCGExVersion.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

PCGEX_CREATE_PROBE_FACTORY(GradientFlow, {}, {})

#if WITH_EDITOR
void UPCGExProbeGradientFlowProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExProbeGradientFlowProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool FPCGExProbeGradientFlow::IsGlobalProbe() const
{
	return true;
}

bool FPCGExProbeGradientFlow::WantsOctree() const
{
	return true;
}

bool FPCGExProbeGradientFlow::Prepare(FPCGExContext* InContext)
{
	if (!FPCGExProbeOperation::Prepare(InContext))
	{
		return false;
	}

	FlowBuffer = PrimaryDataFacade->GetBroadcaster<double>(Config.FlowAttribute, true, false);
	if (!FlowBuffer)
	{
		return false;
	}

	return true;
}

void FPCGExProbeGradientFlow::ProcessAll(TSet<uint64>& OutEdges) const
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
	TArray<TPair<double, int32>> Distances;
	Distances.SetNum(NumPoints);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		if (!CanGenerateRef[i])
		{
			continue;
		}

		int32 BestUphill = INDEX_NONE;
		int32 BestDownhill = INDEX_NONE;
		double BestUphillGradient = 0;
		double BestDownhillGradient = 0;

		const double MaxDistSq = GetSearchRadius(i);
		const double MaxDist = FMath::Sqrt(MaxDistSq);
		const FVector Pos = Positions[i];
		const double CurrentFlow = FlowBuffer->Read(i);

		Octree->FindElementsWithBoundsTest(
			FBox(Positions[i] + FVector(-MaxDist), Positions[i] + FVector(MaxDist)),
			[&](const PCGExOctree::FItem& Other)
			{
				const int32 OtherIndex = Other.Index;
				if (i == OtherIndex || !AcceptConnectionsRef[OtherIndex])
				{
					return;
				}

				const double DistSq = FVector::DistSquared(Pos, Positions[OtherIndex]);

				if (DistSq > MaxDistSq)
				{
					return;
				}

				const double Dist = FMath::Sqrt(DistSq);
				const double ValueDiff = FlowBuffer->Read(OtherIndex) - CurrentFlow;
				const double Gradient = ValueDiff / Dist;

				if (Config.bSteepestOnly)
				{
					if (Gradient > BestUphillGradient)
					{
						BestUphillGradient = Gradient;
						BestUphill = OtherIndex;
					}
					if (Gradient < BestDownhillGradient)
					{
						BestDownhillGradient = Gradient;
						BestDownhill = OtherIndex;
					}
				}
				else
				{
					// Connect to all neighbors with positive gradient (or all if not uphill only)
					if (!Config.bUphillOnly || ValueDiff > 0)
					{
						OutEdges.Add(PCGEx::H64U(i, OtherIndex));
					}
				}
			});

		if (Config.bSteepestOnly)
		{
			if (BestUphill != INDEX_NONE)
			{
				OutEdges.Add(PCGEx::H64U(i, BestUphill));
			}
			if (!Config.bUphillOnly && BestDownhill != INDEX_NONE)
			{
				OutEdges.Add(PCGEx::H64U(i, BestDownhill));
			}
		}
	}
}
