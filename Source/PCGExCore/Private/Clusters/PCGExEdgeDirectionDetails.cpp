// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Clusters/PCGExEdgeDirectionDetails.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExContext.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/Utils/PCGExDataPreloader.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

void FPCGExEdgeDirectionSettings::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader, const TArray<FPCGExSortRuleConfig>* InSortingRules) const
{
	// EndpointsSort reads the sorting-rule attributes on the vtx facade. DirSourceAttribute belongs to
	// EdgeDotAttribute, which reads it from the EDGE facade -- never through this (vtx) preloader.
	if (DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort && InSortingRules)
	{
		for (const FPCGExSortRuleConfig& Rule : *InSortingRules)
		{
			if (!Rule.bReadDataTag)
			{
				FacadePreloader.Register<double>(InContext, Rule.Selector);
			}
		}
	}
}

bool FPCGExEdgeDirectionSettings::Init(FPCGExContext* InContext, const TSharedRef<PCGExData::FFacade>& InVtxDataFacade, const TArray<FPCGExSortRuleConfig>* InSortingRules, const bool bQuiet)
{
	bAscendingDesired = DirectionChoice == EPCGExEdgeDirectionChoice::SmallestToGreatest;
	if (DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort)
	{
		if (!InSortingRules)
		{
			return false;
		}

		Sorter = MakeShared<PCGExSorting::FSorter>(InContext, InVtxDataFacade, *InSortingRules);
		// The sorter stays Ascending: it answers "does Start sort before End", and bAscendingDesired applies
		// DirectionChoice in SortEndpoints. Deriving both from the choice makes them cancel out.
		Sorter->SortDirection = EPCGExSortDirection::Ascending;
		if (!Sorter->Init(InContext))
		{
			return false;
		}
	}
	return true;
}

bool FPCGExEdgeDirectionSettings::InitFromParent(FPCGExContext* InContext, const FPCGExEdgeDirectionSettings& ParentSettings, const TSharedRef<PCGExData::FFacade>& InEdgeDataFacade, const bool bQuiet)
{
	DirectionMethod = ParentSettings.DirectionMethod;
	DirectionChoice = ParentSettings.DirectionChoice;

	bAscendingDesired = ParentSettings.bAscendingDesired;
	Sorter = ParentSettings.Sorter;

	if (DirectionMethod == EPCGExEdgeDirectionMethod::EdgeDotAttribute)
	{
		EdgeDirReader = InEdgeDataFacade->GetBroadcaster<FVector>(DirSourceAttribute, true);
		if (!EdgeDirReader)
		{
			if (!bQuiet)
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Dir Source (Edges), DirSourceAttribute)
			}
			return false;
		}
	}

	return true;
}

bool FPCGExEdgeDirectionSettings::SortEndpoints(const PCGExClusters::FCluster* InCluster, PCGExGraphs::FEdge& InEdge) const
{
	const uint32 Start = InEdge.Start;
	const uint32 End = InEdge.End;

	bool bAscending = true;

	if (DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsOrder)
	{
	}
	else if (DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsIndices)
	{
		bAscending = (Start < End);
	}
	else if (DirectionMethod == EPCGExEdgeDirectionMethod::EndpointsSort)
	{
		bAscending = Sorter->Sort(Start, End);
	}
	else if (DirectionMethod == EPCGExEdgeDirectionMethod::EdgeDotAttribute && InEdge.Index != -1)
	{
		const FVector A = InCluster->VtxPoints->GetTransform(Start).GetLocation();
		const FVector B = InCluster->VtxPoints->GetTransform(End).GetLocation();

		const FVector& EdgeDir = (A - B).GetSafeNormal();
		const FVector& CounterDir = EdgeDirReader->Read(InEdge.Index);
		bAscending = CounterDir.Dot(EdgeDir * -1) < CounterDir.Dot(EdgeDir); // TODO : Do we really need both dots?
	}

	if (bAscending != bAscendingDesired)
	{
		InEdge.Start = End;
		InEdge.End = Start;
		return true;
	}

	return false;
}

bool FPCGExEdgeDirectionSettings::SortExtrapolation(const PCGExClusters::FCluster* InCluster, const int32 InEdgeIndex, const int32 StartNodeIndex, const int32 EndNodeIndex) const
{
	PCGExGraphs::FEdge ChainDir = PCGExGraphs::FEdge(InEdgeIndex, InCluster->GetNodePointIndex(StartNodeIndex), InCluster->GetNode(EndNodeIndex)->PointIndex);
	return SortEndpoints(InCluster, ChainDir);
}
