// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindClusterHull.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExFindClusterHull"
#define PCGEX_NAMESPACE FindClusterHull

TArray<FPCGPinProperties> UPCGExFindClusterHullSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindClusterHullSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cluster hulls as closed paths", Required)
	PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cluster hull OBB bounds as points", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExFindClusterHullSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFindClusterHullSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(FindClusterHull)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FindClusterHull)

bool FPCGExFindClusterHullElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindClusterHull)

	PCGEX_FWD(Artifacts)
	if (!Context->Artifacts.Init(Context))
	{
		return false;
	}

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = PCGExCells::OutputLabels::Paths;

	Context->OutputCellBounds = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputCellBounds->OutputPin = PCGExCells::OutputLabels::CellBounds;

	return true;
}

bool FPCGExFindClusterHullElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindClusterHullElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindClusterHull)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
				NewBatch->SetProjectionDetails(Settings->ProjectionDetails);
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;

	// Stage Paths output, disable pin if empty or disabled
	if (!Settings->Artifacts.bOutputPaths || !Context->OutputPaths->StageOutputs())
	{
		Mask |= 1ULL << 0;
	}

	// Stage CellBounds output, disable pin if empty or disabled
	if (!Settings->Artifacts.bOutputCellBounds || !Context->OutputCellBounds->StageOutputs())
	{
		Mask |= 1ULL << 1;
	}

	return Context->TryComplete();
}


namespace PCGExFindClusterHull
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindClusterHull::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		CellsConstraints = MakeShared<PCGExClusters::FCellConstraints>(Settings->Constraints);
		CellsConstraints->BuildWrapperCell(Cluster.ToSharedRef(), ProjectionDetails);

		if (!CellsConstraints->WrapperCell)
		{
			if (!Settings->bQuietFailedToFindHullWarning)
			{
				PCGE_LOG_C(Warning, GraphAndLog, Context, FTEXT("Failed to find the hull of a cluster."));
			}
			return false;
		}

		TArray<TSharedPtr<PCGExClusters::FCell>> HullArray;
		HullArray.Add(CellsConstraints->WrapperCell);

		if (Settings->Artifacts.bOutputCellBounds)
		{
			TSharedPtr<PCGExData::FPointIO> OBBPointIO = Context->OutputCellBounds->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
			if (!OBBPointIO)
			{
				return false;
			}

			OBBPointIO->IOIndex = EdgeDataFacade->Source->IOIndex;
			PCGExClusters::Helpers::CleanupClusterData(OBBPointIO);

			PCGEX_MAKE_SHARED(OBBFacade, PCGExData::FFacade, OBBPointIO.ToSharedRef())
			PCGExClusters::ProcessCellsAsOBBPoints(Cluster, HullArray, OBBFacade,
			                                       Context->Artifacts, TaskManager);
		}

		if (Settings->Artifacts.bOutputPaths)
		{
			ProcessCell(CellsConstraints->WrapperCell);
		}

		CellsConstraints->Cleanup();
		CellsConstraints.Reset();

		return true;
	}

	void FProcessor::ProcessCell(const TSharedPtr<PCGExClusters::FCell>& InCell)
	{
		const TSharedPtr<PCGExData::FPointIO> PathIO = Context->OutputPaths->Emplace_GetRef<UPCGPointArrayData>(VtxDataFacade->Source, PCGExData::EIOInit::New);
		if (!PathIO)
		{
			return;
		}

		PathIO->Tags->Reset();                                          // Tag forwarding handled by artifacts
		PathIO->IOIndex = Cluster->GetEdge(InCell->Seed.Edge)->IOIndex; // Enforce seed order for collection output-ish

		PCGExClusters::Helpers::CleanupClusterData(PathIO);

		PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, PathIO.ToSharedRef())

		TArray<int32> ReadIndices;
		ReadIndices.SetNumUninitialized(InCell->Nodes.Num());

		for (int i = 0; i < InCell->Nodes.Num(); i++)
		{
			ReadIndices[i] = Cluster->GetNodePointIndex(InCell->Nodes[i]);
		}
		PathIO->InheritPoints(ReadIndices, 0);
		InCell->PostProcessPoints(PathIO->GetOut());

		PCGExPaths::Helpers::SetClosedLoop(PathDataFacade->GetOut(), true);

		Context->Artifacts.Process(Cluster, PathDataFacade, InCell);
		PathDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
