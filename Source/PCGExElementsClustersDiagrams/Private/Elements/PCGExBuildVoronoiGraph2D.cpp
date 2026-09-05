// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildVoronoiGraph2D.h"


#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExRandomHelpers.h"


#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExEdge.h"
#include "Core/PCGExMT.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGExClusterData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/Geo/PCGExDelaunay.h"
#include "Math/Geo/PCGExVoronoi.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE BuildVoronoiGraph2D

bool FPCGExVoronoiSitesOutputDetails::Validate(FPCGExContext* InContext) const
{
	if (bWriteInfluencesCount)
	{
		PCGEX_VALIDATE_NAME_C(InContext, InfluencesCountAttributeName)
	}
	if (bWriteMinRadius)
	{
		PCGEX_VALIDATE_NAME_C(InContext, MinRadiusAttributeName)
	}
	if (bWriteMaxRadius)
	{
		PCGEX_VALIDATE_NAME_C(InContext, MaxRadiusAttributeName)
	}
	return true;
}

void FPCGExVoronoiSitesOutputDetails::Init(const TSharedPtr<PCGExData::FFacade>& InSiteFacade)
{
	InTransforms = InSiteFacade->GetIn()->GetConstTransformValueRange();
	const int32 NumSites = InTransforms.Num();

	Locations.Init(FVector::ZeroVector, NumSites);
	Influences.Init(0, NumSites);

	if (bWriteMinRadius)
	{
		MinRadiusWriter = InSiteFacade->GetWritable<double>(MinRadiusAttributeName, 0, true, PCGExData::EBufferInit::New);
		MinRadius = StaticCastSharedPtr<PCGExData::TArrayBuffer<double>>(MinRadiusWriter)->GetOutValues();
		bWantsDist = true;
	}

	if (bWriteMaxRadius)
	{
		MaxRadiusWriter = InSiteFacade->GetWritable<double>(MaxRadiusAttributeName, 0, true, PCGExData::EBufferInit::New);
		MaxRadius = StaticCastSharedPtr<PCGExData::TArrayBuffer<double>>(MaxRadiusWriter)->GetOutValues();
		bWantsDist = true;
	}

	if (bWriteInfluencesCount)
	{
		InfluenceCountWriter = InSiteFacade->GetWritable<int32>(InfluencesCountAttributeName, 0, true, PCGExData::EBufferInit::New);
	}
}

void FPCGExVoronoiSitesOutputDetails::AddInfluence(const int32 SiteIndex, const FVector& SitePosition, const int32 Count)
{
	Locations[SiteIndex] += SitePosition * Count;
	Influences[SiteIndex] += Count;

	if (bWantsDist)
	{
		// Min/max are unaffected by multiplicity, so the distance is computed only once.
		const double Dist = FVector::Distance(SitePosition, InTransforms[SiteIndex].GetLocation());

		if (bWriteMinRadius)
		{
			double& Min = *(MinRadius->GetData() + SiteIndex);
			Min = FMath::Min(Min, Dist);
		}

		if (bWriteMaxRadius)
		{
			double& Max = *(MaxRadius->GetData() + SiteIndex);
			Max = FMath::Max(Max, Dist);
		}
	}
}

void FPCGExVoronoiSitesOutputDetails::Output(const int32 SiteIndex)
{
	if (InfluenceCountWriter)
	{
		InfluenceCountWriter->SetValue(SiteIndex, Influences[SiteIndex]);
	}
}

TArray<FPCGPinProperties> UPCGExBuildVoronoiGraph2DSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	if (bOutputSites)
	{
		PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputSitesLabel, "Updated Delaunay sites.", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BuildVoronoiGraph2D)
PCGEX_ELEMENT_BATCH_POINT_IMPL(BuildVoronoiGraph2D)

bool FPCGExBuildVoronoiGraph2DElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BuildVoronoiGraph2D)

	if (!Settings->SitesOutputDetails.Validate(Context))
	{
		return false;
	}

	if (Settings->bOutputSites)
	{
		if (!Settings->bPruneOpenSites)
		{
			PCGEX_VALIDATE_NAME(Settings->OpenSiteFlag)
		}

		Context->SitesOutput = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->SitesOutput->OutputPin = PCGExClusters::Labels::OutputSitesLabel;

		for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
		{
			Context->SitesOutput->Emplace_GetRef(IO, PCGExData::EIOInit::NoInit);
		}
	}

	return true;
}

bool FPCGExBuildVoronoiGraph2DElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildVoronoiGraph2DElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildVoronoiGraph2D)
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
				NewBatch->bRequiresWriteStep = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any valid inputs to build from."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	if (Context->SitesOutput)
	{
		Context->SitesOutput->StageOutputs();
	}
	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExBuildVoronoiGraph2D
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildVoronoiGraph2D::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Initialize the cluster output once, up-front; on failure an empty output is staged
		if (!PointDataFacade->Source->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New))
		{
			return false;
		}

		SitesOutputDetails = Settings->SitesOutputDetails;

		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(PointDataFacade))
		{
			return false;
		}

		// Build voronoi

		TArray<FVector> ActivePositions;
		PCGExPointArrayDataHelpers::PointsToPositions(PointDataFacade->GetIn(), ActivePositions);

		Voronoi = MakeShared<PCGExMath::Geo::TVoronoi2>();

		const FBox Bounds = PointDataFacade->GetIn()->GetBounds().ExpandBy(Settings->ExpandBounds);

		// Use metric-aware processing for all metrics (uses 2D circumcenters for correct top-view)
		const EPCGExVoronoiMetric Metric = Settings->Metric;
		const bool bSuccess = Voronoi->Process(ActivePositions, ProjectionDetails, Bounds, WithinBounds, Metric, Settings->Method);

		if (!bSuccess)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some inputs generated invalid results."));
			return false;
		}

		return BuildOutputs();
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
	}

	void FProcessor::CompleteWork()
	{
		if (!GraphBuilder->bCompiledSuccessfully)
		{
			bIsProcessorValid = false;
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
			return;
		}

		if (SiteDataFacade)
		{
			SiteDataFacade->WriteFastest(TaskManager);
			if (Settings->bPruneOpenSites)
			{
				const int32 Iterations = SiteDataFacade->GetOut()->GetNumPoints();

				TArray<int8> Mask;
				Mask.Init(0, Iterations);

				for (int32 i = 0; i < Iterations; i++)
				{
					if (IsVtxValid[i])
					{
						Mask[i] = 1;
					}
				}

				(void)SiteDataFacade->Source->Gather(Mask);
			}
		}

		if (SiteDataFacade)
		{
			SiteDataFacade->Source->Tags->Append(PointDataFacade->Source->Tags.ToSharedRef());
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

	bool FProcessor::BuildOutputs()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildVoronoiGraph2D::BuildOutputs);

		// OutputVertices holds cell centers followed by L1/Linf bend points (if any);
		// OutputEdges holds the matching (subdivided) edges
		const int32 NumOutputVertices = Voronoi->OutputVertices.Num();
		const int32 NumCellCenters = Voronoi->NumCellCenters;
		const int32 NumInputPoints = PointDataFacade->GetNum(PCGExData::EIOSide::In);

		if (NumOutputVertices == 0)
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Voronoi produced no output vertices."));
			return false;
		}

		// Setup site output if needed (based on original Delaunay sites, not Voronoi vertices)
		if (Settings->bOutputSites)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildVoronoiGraph2D::SitesSetup);

			IsVtxValid.Init(true, NumInputPoints);
			for (const int32 HullVtx : Voronoi->Delaunay->DelaunayHull)
			{
				IsVtxValid[HullVtx] = false;
			}

			SiteDataFacade = MakeShared<PCGExData::FFacade>(Context->SitesOutput->Pairs[PointDataFacade->Source->IOIndex].ToSharedRef());
			PCGEX_INIT_IO(SiteDataFacade->Source, PCGExData::EIOInit::Duplicate)
			SiteDataFacade->GetOut()->AllocateProperties(EPCGPointNativeProperties::Transform);

			SitesOutputDetails.Init(SiteDataFacade);

			// Update site influence & validity from cell adjacency. Each cell center
			// influences its triangle corners once per adjacent cell - identical to
			// walking the unique cell-to-cell edges from both endpoints.
			const TArray<PCGExMath::Geo::FDelaunaySite2>& Sites = Voronoi->Delaunay->Sites;
			const TArray<FVector>& CellCenters = Voronoi->OutputVertices;

			for (const PCGExMath::Geo::FDelaunaySite2& Site : Sites)
			{
				int32 NumAdjacent = 0;
				for (int i = 0; i < 3; i++)
				{
					if (Site.Neighbors[i] != -1)
					{
						NumAdjacent++;
					}
				}

				if (NumAdjacent == 0)
				{
					continue;
				}

				const FVector& SitePos = CellCenters[Site.Id];
				for (int i = 0; i < 3; i++)
				{
					SitesOutputDetails.AddInfluence(Site.Vtx[i], SitePos, NumAdjacent);
				}

				if (!WithinBounds[Site.Id])
				{
					for (int i = 0; i < 3; i++)
					{
						IsVtxValid[Site.Vtx[i]] = false;
					}
				}
			}

			if (!Settings->bPruneOpenSites)
			{
				OpenSiteWriter = SiteDataFacade->GetWritable<bool>(Settings->OpenSiteFlag, PCGExData::EBufferInit::New);
			}
		}

		UPCGBasePointData* OutputPoints = PointDataFacade->GetOut();
		(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(OutputPoints, NumOutputVertices, PointDataFacade->GetAllocations());

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildVoronoiGraph2D::WriteVertices);

			TPCGValueRange<FTransform> OutTransforms = OutputPoints->GetTransformValueRange(true);
			TPCGValueRange<int32> OutSeeds = OutputPoints->GetSeedValueRange(true);

			const TArray<FVector>& OutputVertices = Voronoi->OutputVertices;

			PCGEX_PARALLEL_FOR(
				NumOutputVertices,
				const FVector& Pos = OutputVertices[i];
				OutTransforms[i].SetLocation(Pos);
				OutSeeds[i] = PCGExRandomHelpers::ComputeSpatialSeed(Pos);
				)
		}

		// Build graph from OutputEdges. Edges are unique by construction (unique site
		// pairs + fresh bend indices), so adopt them directly - no per-edge dedup.
		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildVoronoiGraph2D::AdoptEdges);

			const TArray<uint64>& OutputEdges = Voronoi->OutputEdges;

			TArray<PCGExGraphs::FEdge> GraphEdges;
			GraphEdges.SetNumUninitialized(OutputEdges.Num());

			PCGEX_PARALLEL_FOR(
				GraphEdges.Num(),
				uint32 A = 0;
				uint32 B = 0;
				PCGEx::H64(OutputEdges[i], A, B);
				GraphEdges[i] = PCGExGraphs::FEdge(i, A, B, -1, -1);
				)

			// Compile-only graph: skip the dedup map, per-node links and metadata sizing
			GraphBuilder->Graph->AdoptEdges(GraphEdges, false);
		}

		// Mark out-of-bounds cell centers as invalid - the builder will handle pruning
		if (Settings->bPruneOutOfBounds)
		{
			for (int32 i = 0; i < NumCellCenters; i++)
			{
				if (!WithinBounds[i])
				{
					GraphBuilder->Graph->Nodes[i].bValid = false;
				}
			}
		}

		Voronoi.Reset();

		GraphBuilder->bInheritNodeData = false;
		// Edges were inserted sequentially in deterministic order (ascending parent edge
		// index); skip the per-subgraph edge key sort.
		GraphBuilder->bSortEdgeKeys = false;
		GraphBuilder->CompileAsync(TaskManager, false);

		// Process site output asynchronously if needed
		if (Settings->bOutputSites)
		{
			PCGEX_ASYNC_GROUP_CHKD(TaskManager, OutputSites)

			OutputSites->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS

				TPCGValueRange<FTransform> SiteOutTransforms = This->SiteDataFacade->GetOut()->GetTransformValueRange(false);

				const TArray<FVector>& SitesPositionsDetails = This->SitesOutputDetails.Locations;
				const TArray<int32>& SitesInfluenceCountDetails = This->SitesOutputDetails.Influences;

				PCGEX_SCOPE_LOOP(Index)
				{
					if (This->OpenSiteWriter)
					{
						// IsVtxValid marks a CLOSED cell; the flag names the open ones.
						This->OpenSiteWriter->SetValue(Index, !This->IsVtxValid[Index]);
					}
					This->SitesOutputDetails.Output(Index);
					if (SitesInfluenceCountDetails[Index] == 0)
					{
						continue;
					}
					SiteOutTransforms[Index].SetLocation(SitesPositionsDetails[Index] / SitesInfluenceCountDetails[Index]);
				}
			};

			OutputSites->StartSubLoops(NumInputPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
		}

		return true;
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
