// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExFloodFillEdgeDirection.h"

#include "PCGElement.h"
#include "Clusters/PCGExCluster.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExData.h"
#include "Helpers/PCGExMetaHelpers.h"

void FPCGExFloodFillEdgeDirectionDetails::ValidateNames(FPCGExContext* InContext)
{
	PCGEX_SOFT_VALIDATE_NAME(bOutputDirection, DirectionAttributeName, InContext)
}

PCGExData::EIOInit FPCGExFloodFillEdgeDirectionDetails::ResolveEdgeInitMode(bool bWantsStealing) const
{
	// Edges only need to be writable (a copy, unless stealing) when we actually output a direction.
	if (!bOutputDirection)
	{
		return PCGExData::EIOInit::Forward;
	}
	return bWantsStealing ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

void FPCGExFloodFillEdgeDirectionDetails::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	if (!bOutputDirection)
	{
		return;
	}
	DirectionSettings.RegisterBuffersDependencies(InContext, FacadePreloader);
}

bool FPCGExFloodFillEdgeDirectionDetails::InitForBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TArray<FPCGExSortRuleConfig>* InSortingRules)
{
	if (!bOutputDirection)
	{
		return true;
	}
	return DirectionSettings.Init(InContext, InVtxDataFacade, InSortingRules);
}

bool FPCGExFloodFillEdgeDirectionDetails::InitForProcessor(FPCGExContext* InContext, const FPCGExFloodFillEdgeDirectionDetails& InParent, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade)
{
	if (!bOutputDirection)
	{
		return true;
	}

	DirectionWriter = InEdgeDataFacade->GetWritable<FVector>(DirectionAttributeName, FVector::ZeroVector, true, PCGExData::EBufferInit::Inherit);
	if (!DirectionWriter)
	{
		return false;
	}

	WritePtr = StaticCastSharedPtr<PCGExData::TArrayBuffer<FVector>>(DirectionWriter)->GetOutValues()->GetData();

	return DirectionSettings.InitFromParent(InContext, InParent.DirectionSettings, InEdgeDataFacade);
}

void FPCGExFloodFillEdgeDirectionDetails::WriteFromNodeDepths(const PCGExClusters::FCluster* InCluster, const TArray<int32>& InNodeDepths) const
{
	check(WritePtr); // Callers gate on IsActive().

	// Each edge is sorted as a copy: the cluster's edges can be shared with other consumers of a cached cluster.
	const TArray<PCGExGraphs::FEdge>& Edges = *InCluster->Edges;
	for (PCGExGraphs::FEdge Edge : Edges)
	{
		const int32 StartNode = InCluster->GetEdgeStart(Edge)->Index;
		const int32 EndNode = InCluster->GetEdgeEnd(Edge)->Index;

		const int32 StartDepth = InNodeDepths[StartNode];
		const int32 EndDepth = InNodeDepths[EndNode];

		if (StartDepth >= 0 && EndDepth >= 0 && StartDepth != EndDepth)
		{
			// Both endpoints visited at different depths -> orient shallow to deep.
			const bool bStartFirst = StartDepth < EndDepth;
			Set(Edge.PointIndex, InCluster->GetDir(bStartFirst ? StartNode : EndNode, bStartFirst ? EndNode : StartNode));
		}
		else
		{
			// No depth difference to exploit -> fall back to the Edge Direction Settings (Edge Properties logic).
			DirectionSettings.SortEndpoints(InCluster, Edge);
			Set(Edge.PointIndex, InCluster->GetEdgeDir(Edge));
		}
	}
}
