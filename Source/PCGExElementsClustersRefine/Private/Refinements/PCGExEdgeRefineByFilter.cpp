// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Refinements/PCGExEdgeRefineByFilter.h"

#pragma region FPCGExEdgeRefineByFilter

void FPCGExEdgeRefineByFilter::PrepareForCluster(const TSharedPtr<PCGExClusters::FCluster>& InCluster, const TSharedPtr<PCGExHeuristics::FHandler>& InHeuristics)
{
	FPCGExEdgeRefineOperation::PrepareForCluster(InCluster, InHeuristics);
	ExchangeValue = bInvert ? 0 : 1;
}

void FPCGExEdgeRefineByFilter::ProcessEdge(PCGExGraphs::FEdge& Edge)
{
	if (*(EdgeFilterCache->GetData() + Edge.Index))
	{
		FPlatformAtomics::InterlockedExchange(&Edge.bValid, ExchangeValue);
	}
}

#pragma endregion
