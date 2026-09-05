// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBuildDelaunayGraph2D.h"

#include "Clusters/PCGExCluster.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"
#include "Math/PCGExBestFitPlane.h"
#include "Math/Geo/PCGExDelaunay.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE BuildDelaunayGraph2D

namespace PCGExGeoTask
{
	class FLloydRelax2;
}

#if WITH_EDITOR
TArray<FText> UPCGExBuildDelaunayGraph2DSettings::GetNodeTitleAliases() const
{
	return {FTEXT("PCGEx | Cluster : Triangulation")};
}
#endif

TArray<FPCGPinProperties> UPCGExBuildDelaunayGraph2DSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	if (bOutputSites)
	{
		PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputSitesLabel, "Complete delaunay sites.", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BuildDelaunayGraph2D)
PCGEX_ELEMENT_BATCH_POINT_IMPL(BuildDelaunayGraph2D)

bool FPCGExBuildDelaunayGraph2DElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BuildDelaunayGraph2D)

	PCGEX_VALIDATE_NAME(Settings->HullAttributeName)

	if (Settings->bOutputSites)
	{
		if (Settings->bMarkSiteHull)
		{
			PCGEX_VALIDATE_NAME(Settings->SiteHullAttributeName)
		}
		Context->MainSites = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->MainSites->OutputPin = PCGExClusters::Labels::OutputSitesLabel;
		Context->MainSites->Pairs.Init(nullptr, Context->MainPoints->Pairs.Num());
	}

	return true;
}

bool FPCGExBuildDelaunayGraph2DElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBuildDelaunayGraph2DElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BuildDelaunayGraph2D)
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
	if (Context->MainSites)
	{
		Context->MainSites->StageOutputs();
	}
	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExBuildDelaunayGraph2D
{
	class FOutputDelaunaySites2D final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FOutputDelaunaySites2D)

		FOutputDelaunaySites2D(const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<FProcessor>& InProcessor)
			: FTask()
			  , PointIO(InPointIO)
			  , Processor(InProcessor)
		{
		}

		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<FProcessor> Processor;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDelaunaySites2D::ExecuteTask);

			FPCGExBuildDelaunayGraph2DContext* Context = TaskManager->GetContext<FPCGExBuildDelaunayGraph2DContext>();
			PCGEX_SETTINGS(BuildDelaunayGraph2D)

			const TSharedPtr<PCGExData::FPointIO> SitesIO = NewPointIO(PointIO.ToSharedRef());
			PCGEX_INIT_IO_VOID(SitesIO, PCGExData::EIOInit::New)

			Context->MainSites->Insert_Unsafe(Processor->BatchIndex, SitesIO);

			const UPCGBasePointData* OriginalPoints = SitesIO->GetIn();
			UPCGBasePointData* MutablePoints = SitesIO->GetOut();

			PCGExMath::Geo::TDelaunay2* Delaunay = Processor->Delaunay.Get();
			const int32 NumSites = Delaunay->Sites.Num();

			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumSites, SitesIO->GetAllocations());
			TArray<int32>& IdxMapping = SitesIO->GetIdxMapping();

			TConstPCGValueRange<FTransform> InTransforms = OriginalPoints->GetConstTransformValueRange();
			TPCGValueRange<FTransform> OutTransforms = MutablePoints->GetTransformValueRange();

			for (int i = 0; i < NumSites; i++)
			{
				const PCGExMath::Geo::FDelaunaySite2& Site = Delaunay->Sites[i];

				FVector Centroid = FVector::ZeroVector;
				for (int j = 0; j < 3; j++)
				{
					Centroid += InTransforms[Site.Vtx[j]].GetLocation();
				}
				Centroid /= 3;

				IdxMapping[i] = Site.Vtx[0];
				OutTransforms[i].SetLocation(Centroid);
			}

			EPCGPointNativeProperties Allocate = EPCGPointNativeProperties::All;
			EnumRemoveFlags(Allocate, EPCGPointNativeProperties::Transform);
			SitesIO->ConsumeIdxMapping(Allocate);

			if (Settings->bMarkSiteHull)
			{
				PCGEX_MAKE_SHARED(HullBuffer, PCGExData::TArrayBuffer<bool>, SitesIO.ToSharedRef(), Settings->SiteHullAttributeName)
				HullBuffer->InitForWrite(false, true, PCGExData::EBufferInit::New);
				{
					TArray<bool>& OutValues = *HullBuffer->GetOutValues();
					for (int i = 0; i < NumSites; i++)
					{
						OutValues[i] = Delaunay->Sites[i].bOnHull;
					}
				}
				WriteBuffer(TaskManager, HullBuffer);
			}
		}
	};

	class FOutputDelaunayUrquhartSites2D final : public PCGExMT::FTask
	{
	public:
		PCGEX_ASYNC_TASK_NAME(FOutputDelaunayUrquhartSites2D)

		FOutputDelaunayUrquhartSites2D(const TSharedPtr<PCGExData::FPointIO>& InPointIO, const TSharedPtr<FProcessor>& InProcessor)
			: FTask()
			  , PointIO(InPointIO)
			  , Processor(InProcessor)
		{
		}

		TSharedPtr<PCGExData::FPointIO> PointIO;
		TSharedPtr<FProcessor> Processor;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FOutputDelaunayUrquhartSites2D::ExecuteTask);

			FPCGExBuildDelaunayGraph2DContext* Context = TaskManager->GetContext<FPCGExBuildDelaunayGraph2DContext>();
			PCGEX_SETTINGS(BuildDelaunayGraph2D)

			TSharedPtr<PCGExData::FPointIO> SitesIO = NewPointIO(PointIO.ToSharedRef());
			PCGEX_INIT_IO_VOID(SitesIO, PCGExData::EIOInit::New)

			Context->MainSites->Insert_Unsafe(Processor->BatchIndex, SitesIO);

			const UPCGBasePointData* OriginalPoints = SitesIO->GetIn();
			UPCGBasePointData* MutablePoints = SitesIO->GetOut();

			PCGExMath::Geo::TDelaunay2* Delaunay = Processor->Delaunay.Get();
			const int32 NumSites = Delaunay->Sites.Num();

			// TODO : Revisit this to avoid allocating so much memory when we only need a subset
			(void)PCGExPointArrayDataHelpers::SetNumPointsAllocated(MutablePoints, NumSites, SitesIO->GetAllocations());
			TArray<int32>& IdxMapping = SitesIO->GetIdxMapping();

			TConstPCGValueRange<FTransform> InTransforms = OriginalPoints->GetConstTransformValueRange();
			TPCGValueRange<FTransform> OutTransforms = MutablePoints->GetTransformValueRange(true);

			TBitArray<> VisitedSites;
			VisitedSites.Init(false, NumSites);

			TBitArray<> Hull;
			TArray<int32> FinalSites;
			FinalSites.Reserve(NumSites);
			Hull.Reserve(NumSites / 4);

			for (int i = 0; i < NumSites; i++)
			{
				if (VisitedSites[i])
				{
					continue;
				}
				VisitedSites[i] = true;

				const PCGExMath::Geo::FDelaunaySite2& Site = Delaunay->Sites[i];

				TSet<int32> QueueSet;
				TSet<uint64> QueuedEdgesSet;
				Delaunay->GetMergedSites(i, Processor->UrquhartEdges, QueueSet, QueuedEdgesSet, VisitedSites);

				if (QueuedEdgesSet.IsEmpty())
				{
					continue;
				}

				TArray<int32> Queue = QueueSet.Array();
				QueueSet.Empty();

				TArray<uint64> QueuedEdges = QueuedEdgesSet.Array();
				QueuedEdgesSet.Empty();

				FVector Centroid = FVector::ZeroVector;
				bool bOnHull = Site.bOnHull;

				if (Settings->UrquhartSitesMerge == EPCGExUrquhartSiteMergeMode::MergeSites)
				{
					for (const int32 MergeSiteIndex : Queue)
					{
						const PCGExMath::Geo::FDelaunaySite2& MSite = Delaunay->Sites[MergeSiteIndex];
						for (int j = 0; j < 3; j++)
						{
							Centroid += InTransforms[MSite.Vtx[j]].GetLocation();
						}

						if (!bOnHull && Settings->bMarkSiteHull && MSite.bOnHull)
						{
							bOnHull = true;
						}
					}

					Centroid /= (Queue.Num() * 3);
				}
				else
				{
					if (Settings->bMarkSiteHull)
					{
						for (const int32 MergeSiteIndex : Queue)
						{
							if (!bOnHull && Delaunay->Sites[MergeSiteIndex].bOnHull)
							{
								bOnHull = true;
								break;
							}
						}
					}

					for (const uint64 EdgeHash : QueuedEdges)
					{
						Centroid += FMath::Lerp(InTransforms[PCGEx::H64A(EdgeHash)].GetLocation(), InTransforms[PCGEx::H64B(EdgeHash)].GetLocation(), 0.5);
					}

					Centroid /= (QueuedEdges.Num());
				}

				const int32 VIndex = FinalSites.Add(Site.Vtx[0]);

				Hull.Add(bOnHull);
				IdxMapping[VIndex] = Site.Vtx[0];
				OutTransforms[VIndex].SetLocation(Centroid);
			}

			IdxMapping.SetNum(FinalSites.Num());
			MutablePoints->SetNumPoints(FinalSites.Num());

			SitesIO->ConsumeIdxMapping(OriginalPoints->GetAllocatedProperties() & ~EPCGPointNativeProperties::Transform);

			if (Settings->bMarkSiteHull)
			{
				PCGEX_MAKE_SHARED(HullBuffer, PCGExData::TArrayBuffer<bool>, SitesIO.ToSharedRef(), Settings->SiteHullAttributeName)
				HullBuffer->InitForWrite(false, true, PCGExData::EBufferInit::New);
				{
					TArray<bool>& OutValues = *HullBuffer->GetOutValues();
					for (int i = 0; i < Hull.Num(); i++)
					{
						OutValues[i] = Hull[i];
					}
				}
				WriteBuffer(TaskManager, HullBuffer);
			}
		}
	};

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBuildDelaunayGraph2D::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		ProjectionDetails = Settings->ProjectionDetails;
		if (!ProjectionDetails.Init(PointDataFacade))
		{
			return false;
		}

		// Build delaunay

		TArray<FVector> ActivePositions;
		PCGExPointArrayDataHelpers::PointsToPositions(PointDataFacade->Source->GetIn(), ActivePositions);

		Delaunay = MakeShared<PCGExMath::Geo::TDelaunay2>();

		if (!Delaunay->Process(ActivePositions, ProjectionDetails))
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some inputs generated invalid results."));
			return false;
		}

		if (!PointDataFacade->Source->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::Duplicate))
		{
			return false;
		}

		if (Settings->bUrquhart)
		{
			if (Settings->bOutputSites && Settings->UrquhartSitesMerge != EPCGExUrquhartSiteMergeMode::None)
			{
				Delaunay->RemoveLongestEdges(ActivePositions, UrquhartEdges);
			}
			else
			{
				Delaunay->RemoveLongestEdges(ActivePositions);
			}
		}

		ActivePositions.Empty();

		PCGEX_SHARED_THIS_DECL
		if (Settings->bOutputSites)
		{
			if (Settings->UrquhartSitesMerge != EPCGExUrquhartSiteMergeMode::None)
			{
				PCGEX_LAUNCH(FOutputDelaunayUrquhartSites2D, PointDataFacade->Source, ThisPtr)
			}
			else
			{
				PCGEX_LAUNCH(FOutputDelaunaySites2D, PointDataFacade->Source, ThisPtr)
			}
		}

		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);

		if (Settings->bMarkHull)
		{
			OutputIndices = MakeShared<TArray<int32>>();
			PCGExArrayHelpers::ArrayOfIndices(*OutputIndices, PointDataFacade->GetNum());
		}

		GraphBuilder->Graph->InsertEdges(Delaunay->DelaunayEdges, -1);
		GraphBuilder->CompileAsync(TaskManager, false);

		if (!Settings->bMarkHull && !Settings->bOutputSites)
		{
			Delaunay.Reset();
		}

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BuildDelaunayGraph3D::ProcessPoints);
		const TArray<int32>& OutputIndicesRef = *OutputIndices.Get();
		PCGEX_SCOPE_LOOP(Index)
		{
			HullMarkPointWriter->SetValue(Index, Delaunay->DelaunayHull.Contains(Index));
		}
	}

	void FProcessor::CompleteWork()
	{
		if (!GraphBuilder)
		{
			return;
		}

		if (!GraphBuilder->bCompiledSuccessfully)
		{
			bIsProcessorValid = false;
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
			return;
		}


		if (Settings->bMarkHull)
		{
			HullMarkPointWriter = PointDataFacade->GetWritable<bool>(Settings->HullAttributeName, false, true, PCGExData::EBufferInit::New);
			StartParallelLoopForPoints();
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
