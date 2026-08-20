// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graphs/PCGExGraphTasks.h"

#include "Clusters/PCGExClustersHelpers.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Data/Utils/PCGExDataForwardDetails.h"
#include "Fitting/PCGExFittingTasks.h"
#include "Graphs/PCGExGraphBuilder.h"

namespace PCGExGraphTask
{
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

		VtxDupe->IOIndex = OutIOIndex;

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

		PCGEX_MAKE_SHARED(VtxTask, PCGExFitting::Tasks::FTransformPointIO, TaskIndex, PointIO, VtxDupe, TransformDetails);
		Launch(VtxTask);

		for (const TSharedPtr<PCGExData::FPointIO>& Edges : GraphBuilder->EdgesIO->Pairs)
		{
			TSharedPtr<PCGExData::FPointIO> EdgeDupe = EdgeCollection->Emplace_GetRef(Edges->GetOut(), PCGExData::EIOInit::Duplicate);
			if (!EdgeDupe)
			{
				return;
			}

			EdgeDupe->IOIndex = OutIOIndex;
			PCGExClusters::Helpers::MarkClusterEdges(EdgeDupe, OutId);

			if (AttributesToTags && PointIO)
			{
				AttributesToTags->Tag(PointIO->GetInPoint(TaskIndex), EdgeDupe);
			}

			PCGEX_MAKE_SHARED(EdgeTask, PCGExFitting::Tasks::FTransformPointIO, TaskIndex, PointIO, EdgeDupe, TransformDetails);
			Launch(EdgeTask);
		}

		// TODO : Copy & Transform the cached cluster as well for a big perf boost
	}
}
