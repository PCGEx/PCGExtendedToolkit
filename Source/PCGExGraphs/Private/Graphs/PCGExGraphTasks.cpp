// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/PCGExGraphTasks.h"

#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Fitting/PCGExFitting.h"
#include "Fitting/PCGExFittingTasks.h"
#include "Graphs/PCGExGraphBuilder.h"

namespace PCGExGraphTask
{
	FBox ComputeClusterFitBounds(const PCGExGraphs::FGraphBuilder& InGraphBuilder, const bool bIgnoreBounds)
	{
		return PCGExFitting::Tasks::ComputeFitBounds(InGraphBuilder.NodeDataFacade->GetOut(), bIgnoreBounds);
	}

	void FCopyGraphToPoint::ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
	{
		if (!GraphBuilder || !GraphBuilder->bCompiledSuccessfully)
		{
			return;
		}

		const TSharedPtr<PCGExData::FPointIO> VtxDupe = VtxCollection->Emplace_GetRef(GraphBuilder->NodeDataFacade->GetOut(), PCGExData::EIOInit::Duplicate);
		if (!VtxDupe)
		{
			return;
		}

		// Copies are emplaced from concurrent tasks: stage by copy, then (edges) by subgraph.
		VtxDupe->IOIndex = OutIOIndex;
		VtxDupe->SetSortKey(OutIOIndex);

		PCGExDataId OutId;
		PCGExClusters::Helpers::SetClusterVtx(VtxDupe, OutId);

		// Tags and forwarded attributes are applied BEFORE the async transform is launched, so the copy
		// carries its target's identity no matter when the transform lands.
		if (AttributesToTags && PointIO)
		{
			AttributesToTags->Tag(PointIO->GetInPoint(TaskIndex), VtxDupe);
		}
		if (ForwardHandler && VtxDupe->GetOut())
		{
			ForwardHandler->Forward(TaskIndex, VtxDupe->GetOut()->Metadata);
		}

		// Edges fit against the Vtx bounds too, so every half of the copy shares one fit.
		const FBox VtxFitBounds = FitBounds.IsSet() ? FitBounds.GetValue() : ComputeClusterFitBounds(*GraphBuilder, TransformDetails->bIgnoreBounds);

		PCGEX_MAKE_SHARED(VtxTask, PCGExFitting::Tasks::FTransformPointIO, TaskIndex, PointIO, VtxDupe, TransformDetails, VtxFitBounds);
		Launch(VtxTask);

		int32 EdgeOrdinal = 0;
		for (const TSharedPtr<PCGExData::FPointIO>& Edges : GraphBuilder->EdgesIO->Pairs)
		{
			// With cluster caching on, DuplicateData rebinds the builder's bound cluster onto the dupe
			// (UPCGExClusterEdgesData::InitializeSpatialDataInternal).
			TSharedPtr<PCGExData::FPointIO> EdgeDupe = EdgeCollection->Emplace_GetRef(Edges->GetOut(), PCGExData::EIOInit::Duplicate);
			if (!EdgeDupe)
			{
				return;
			}

			EdgeDupe->IOIndex = OutIOIndex;
			EdgeDupe->SetSortKey(OutIOIndex, EdgeOrdinal++);
			PCGExClusters::Helpers::MarkClusterEdges(EdgeDupe, OutId);

			if (AttributesToTags && PointIO)
			{
				AttributesToTags->Tag(PointIO->GetInPoint(TaskIndex), EdgeDupe);
			}

			PCGEX_MAKE_SHARED(EdgeTask, PCGExFitting::Tasks::FTransformPointIO, TaskIndex, PointIO, EdgeDupe, TransformDetails, VtxFitBounds);
			Launch(EdgeTask);
		}
	}
}
