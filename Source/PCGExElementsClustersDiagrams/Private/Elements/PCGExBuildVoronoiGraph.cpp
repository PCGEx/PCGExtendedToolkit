// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildVoronoiGraph.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#include "Clusters/PCGExCluster.h"
#include "Data/PCGExClusterData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Math/Geo/PCGExVoronoi.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE BuildVoronoiGraph

namespace PCGExGeoTask
{
	class FLloydRelax3;
}

TArray<FPCGPinProperties> UPCGExBuildVoronoiGraphSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	//PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputSitesLabel, "Complete delaunay sites.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BuildVoronoiGraph)
PCGEX_ELEMENT_BATCH_POINT_IMPL(BuildVoronoiGraph)

bool FPCGExBuildVoronoiGraphElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BuildVoronoiGraph)

	Context->SitesOutput = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->SitesOutput->OutputPin = PCGExClusters::Labels::OutputSitesLabel;

	return true;
}

bool FPCGExBuildVoronoiGraphElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildVoronoiGraphElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildVoronoiGraph)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs have less than 4 points and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 4)
				{
					bHasInvalidInputs = true;
					return false;
				}

				Context->SitesOutput->Emplace_GetRef(Entry, PCGExData::EIOInit::New);
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any valid inputs to build from."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	//Context->SitesOutput->OutputToContext();
	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExBuildVoronoiGraph
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildVoronoiGraph::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Build voronoi

		TArray<FVector> ActivePositions;
		PCGExPointArrayDataHelpers::PointsToPositions(PointDataFacade->Source->GetIn(), ActivePositions);

		Voronoi = MakeShared<PCGExMath::Geo::TVoronoi3>();

		if (!Voronoi->Process(ActivePositions))
		{
			if (!Context->bQuietInvalidInputWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some inputs generated invalid results. Are points coplanar? If so, use Voronoi 2D instead."));
			}
			return false;
		}

		ActivePositions.Empty();

		if (!PointDataFacade->Source->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New))
		{
			return false;
		}
		const FBox Bounds = PointDataFacade->Source->GetIn()->GetBounds().ExpandBy(Settings->ExpandBounds);

		if (Settings->Method == EPCGExCellCenter::Circumcenter && Settings->bPruneOutOfBounds)
		{
			int32 Centroids = 0;

			const int32 NumSites = Voronoi->Centroids.Num();
			TArray<int32> RemappedIndices;
			RemappedIndices.SetNumUninitialized(NumSites);

			for (int i = 0; i < NumSites; i++)
			{
				const FVector Centroid = Voronoi->Circumspheres[i].Center;
				if (!Bounds.IsInside(Centroid))
				{
					RemappedIndices[i] = -1;
					continue;
				}

				RemappedIndices[i] = Centroids++;
			}

			TArray<uint64> ValidEdges;
			ValidEdges.Reserve(Voronoi->VoronoiEdges.Num());

			for (const uint64 Hash : Voronoi->VoronoiEdges)
			{
				const int32 A = RemappedIndices[PCGEx::H64A(Hash)];
				const int32 B = RemappedIndices[PCGEx::H64B(Hash)];
				if (A == -1 || B == -1)
				{
					continue;
				}
				ValidEdges.Add(PCGEx::H64(A, B));
			}

			UPCGBasePointData* CentroidsPoints = PointDataFacade->GetOut();
			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(CentroidsPoints, Centroids, PointDataFacade->GetAllocations());

			TPCGValueRange<FTransform> OutTransforms = CentroidsPoints->GetTransformValueRange(true);

			for (int i = 0; i < RemappedIndices.Num(); i++)
			{
				if (const int32 Idx = RemappedIndices[i];
					Idx != -1)
				{
					OutTransforms[Idx].SetLocation(Voronoi->Circumspheres[i].Center);
				}
			}

			RemappedIndices.Empty();

			Voronoi.Reset();

			GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);
			GraphBuilder->Graph->InsertEdges(ValidEdges, -1);

			ValidEdges.Empty();
		}
		else
		{
			UPCGBasePointData* Centroids = PointDataFacade->GetOut();
			const int32 NumSites = Voronoi->Centroids.Num();
			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(Centroids, NumSites, PointDataFacade->GetAllocations());

			TPCGValueRange<FTransform> OutTransforms = Centroids->GetTransformValueRange(false);

			if (Settings->Method == EPCGExCellCenter::Circumcenter)
			{
				for (int i = 0; i < NumSites; i++)
				{
					OutTransforms[i].SetLocation(Voronoi->Circumspheres[i].Center);
				}
			}
			else if (Settings->Method == EPCGExCellCenter::Centroid)
			{
				for (int i = 0; i < NumSites; i++)
				{
					OutTransforms[i].SetLocation(Voronoi->Centroids[i]);
				}
			}
			else if (Settings->Method == EPCGExCellCenter::Balanced)
			{
				for (int i = 0; i < NumSites; i++)
				{
					FVector Target = Voronoi->Circumspheres[i].Center;
					if (Bounds.IsInside(Target))
					{
						OutTransforms[i].SetLocation(Target);
					}
					else
					{
						OutTransforms[i].SetLocation(Voronoi->Centroids[i]);
					}
				}
			}

			GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);
			GraphBuilder->Graph->InsertEdges(Voronoi->VoronoiEdges, -1);

			//ExtractValidSites();
			Voronoi.Reset();
		}

		// Update seeds

		const int32 NumSites = PointDataFacade->GetOut()->GetNumPoints();
		TPCGValueRange<FTransform> OutTransforms = PointDataFacade->GetOut()->GetTransformValueRange(false);
		TPCGValueRange<int32> OutSeeds = PointDataFacade->GetOut()->GetSeedValueRange(false);

		for (int i = 0; i < NumSites; i++)
		{
			OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[i].GetLocation());
		}

		// Compile graph

		GraphBuilder->bInheritNodeData = false; // We're creating new points from scratch, we don't want the inheritance.
		GraphBuilder->CompileAsync(TaskManager, false);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		//HullMarkPointWriter->Values[Index] = Voronoi->Delaunay->DelaunayHull.Contains(Index);
	}

	void FProcessor::CompleteWork()
	{
		if (!GraphBuilder->bCompiledSuccessfully)
		{
			bIsProcessorValid = false;
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
		}
	}

	void FProcessor::Write()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Output()
	{
		GraphBuilder->StageEdgesOutputs();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
