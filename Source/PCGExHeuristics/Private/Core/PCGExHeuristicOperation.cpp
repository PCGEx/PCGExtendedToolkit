// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Core/PCGExHeuristicOperation.h"

#include "Clusters/PCGExCluster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


void FPCGExHeuristicOperation::PrepareForCluster(const TSharedPtr<const PCGExClusters::FCluster>& InCluster)
{
	Cluster = InCluster;
	LocalWeightMultiplier.Empty();

	bHasCustomLocalWeightMultiplier = false;
	if (bUseLocalWeightMultiplier)
	{
		const TSharedPtr<PCGExData::FPointIO> PointIO = LocalWeightMultiplierSource == EPCGExClusterElement::Vtx ? InCluster->VtxIO.Pin() : InCluster->EdgesIO.Pin();
		const TSharedPtr<PCGExData::FFacade> DataFacade = LocalWeightMultiplierSource == EPCGExClusterElement::Vtx ? PrimaryDataFacade : SecondaryDataFacade;

		const int32 NumPoints = PointIO->GetNum();
		const TSharedPtr<PCGExData::TBuffer<double>> LocalWeightCache = DataFacade->GetBroadcaster<double>(WeightMultiplierAttribute);

		if (!LocalWeightCache)
		{
			PCGEX_LOG_INVALID_SELECTOR_C(Context, Weight Multiplier (Heuristics), WeightMultiplierAttribute)
			return;
		}

		if (LocalWeightMultiplierSource == EPCGExClusterElement::Vtx)
		{
			LocalWeightMultiplier.SetNumZeroed(InCluster->Nodes->Num());
			for (const PCGExClusters::FNode& Node : (*InCluster->Nodes))
			{
				LocalWeightMultiplier[Node.Index] = LocalWeightCache->Read(Node.PointIndex);
			}
		}
		else
		{
			LocalWeightMultiplier.SetNumZeroed(NumPoints);
			for (int i = 0; i < NumPoints; i++)
			{
				LocalWeightMultiplier[i] = LocalWeightCache->Read(i);
			}
		}

		bHasCustomLocalWeightMultiplier = true;
	}
}

double FPCGExHeuristicOperation::GetGlobalScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal) const
{
	return GetScoreInternal(0);
}

double FPCGExHeuristicOperation::GetEdgeScore(const PCGExClusters::FNode& From, const PCGExClusters::FNode& To, const PCGExGraphs::FEdge& Edge, const PCGExClusters::FNode& Seed, const PCGExClusters::FNode& Goal, PCGEx::FHashLookup* TravelStack) const
{
	return GetScoreInternal(0);
}

double FPCGExHeuristicOperation::GetCustomWeightMultiplier(const int32 PointIndex, const int32 EdgeIndex) const
{
	//TODO Rewrite this
	if (!bUseLocalWeightMultiplier || LocalWeightMultiplier.IsEmpty())
	{
		return 1;
	}
	return FMath::Abs(LocalWeightMultiplier[LocalWeightMultiplierSource == EPCGExClusterElement::Vtx ? PointIndex : EdgeIndex]);
}

double FPCGExHeuristicOperation::GetScoreInternal(const double InTime) const
{
	return FMath::Max(0, ScoreCurve->Eval(bInvert ? 1 - InTime : InTime)) * ReferenceWeight;
}
