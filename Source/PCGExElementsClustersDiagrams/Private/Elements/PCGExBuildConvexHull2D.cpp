// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildConvexHull2D.h"


#include "Clusters/PCGExCluster.h"
#include "Curve/CurveUtil.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Math/ConvexHull2d.h"
#include "Math/PCGExBestFitPlane.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE BuildConvexHull2D

TArray<FPCGPinProperties> UPCGExBuildConvexHull2DSettings::OutputPinProperties() const
{
	if (!bOutputClusters)
	{
		TArray<FPCGPinProperties> PinProperties;
		PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Point data representing closed convex hull paths.", Required)
		return PinProperties;
	}
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Point data representing closed convex hull paths.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BuildConvexHull2D)
PCGEX_ELEMENT_BATCH_POINT_IMPL(BuildConvexHull2D)

bool FPCGExBuildConvexHull2DElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BuildConvexHull2D)

	Context->PathsIO = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->PathsIO->OutputPin = PCGExPaths::Labels::OutputPathsLabel;

	return true;
}

bool FPCGExBuildConvexHull2DElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildConvexHull2DElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildConvexHull2D)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 3 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 3)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any valid inputs to build from."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->bOutputClusters)
	{
		Context->MainPoints->StageOutputs();
		Context->MainBatch->Output(); // Edges in order
	}

	Context->PathsIO->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExBuildConvexHull2D
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildConvexHull2D::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(PointDataFacade))
		{
			return false;
		}

		// Build convex hull

		TArray<FVector> ActivePositions;
		PCGExPointArrayDataHelpers::PointsToPositions(PointDataFacade->Source->GetIn(), ActivePositions);
		ProjectionDetails.Project(ActivePositions, ActivePositions);

		TArray<int32> ConvexHullIndices;
		ConvexHull2D::ComputeConvexHull(ActivePositions, ConvexHullIndices);

		const int32 LastIndex = ConvexHullIndices.Num() - 1;
		if (LastIndex < 0)
		{
			// Not enough points to build a convex hull
			return false;
		}

		const TSharedPtr<PCGExData::FPointIO> PathIO = Context->PathsIO->Emplace_GetRef<UPCGPointArrayData>(PointDataFacade->GetIn(), PCGExData::EIOInit::New);
		if (!PathIO)
		{
			return false;
		}

		PathIO->IOIndex = PointDataFacade->Source->IOIndex;
		UPCGBasePointData* MutablePoints = PathIO->GetOut();
		TArray<FVector2D> ProjectedPoints;

		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, LastIndex + 1, PointDataFacade->GetAllocations());
		ProjectedPoints.Reserve(LastIndex + 1);

		PCGExPaths::Helpers::SetClosedLoop(PathIO->GetOut(), true);

		for (int i = 0; i <= LastIndex; i++)
		{
			ProjectedPoints.Emplace(ActivePositions[ConvexHullIndices[i]]);
		}
		if (!PCGExMath::IsWinded(Settings->Winding, UE::Geometry::CurveUtil::SignedArea2<double, FVector2D>(ProjectedPoints) < 0))
		{
			Algo::Reverse(ConvexHullIndices);
		}

		if (Settings->bOutputClusters)
		{
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::New)

			GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);

			PCGExGraphs::FEdge E;
			for (int i = 0; i <= LastIndex; i++)
			{
				const int32 CurrentIndex = ConvexHullIndices[i];
				const int32 NextIndex = ConvexHullIndices[i == LastIndex ? 0 : i + 1];

				GraphBuilder->Graph->InsertEdge(CurrentIndex, NextIndex, E);
			}

			PointDataFacade->Source->InheritPoints(ConvexHullIndices, 0);
			PathIO->InheritPoints(ConvexHullIndices, 0);

			ActivePositions.Empty();

			GraphBuilder->CompileAsync(TaskManager, true);
		}
		else
		{
			PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::New)
			PathIO->InheritPoints(ConvexHullIndices, 0);
			ActivePositions.Empty();
		}

		return true;
	}

	void FProcessor::CompleteWork()
	{
		if (!GraphBuilder || !GraphBuilder->bCompiledSuccessfully)
		{
			bIsProcessorValid = false;
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
		}
	}

	void FProcessor::Output()
	{
		if (!Settings->bOutputClusters)
		{
			return;
		}
		GraphBuilder->StageEdgesOutputs();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
