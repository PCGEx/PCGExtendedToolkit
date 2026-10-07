// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindAllCells.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Clusters/Artifacts/PCGExCellPathBuilder.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExFindAllCells"
#define PCGEX_NAMESPACE FindAllCells

TArray<FPCGPinProperties> UPCGExFindAllCellsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExClusters::Labels::SourceHolesLabel, "Omit cells that contain any points from this dataset", Normal)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindAllCellsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cell contours as closed paths", Required)
	PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cell OBB bounds as points", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExFindAllCellsSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFindAllCellsSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(FindAllCells)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FindAllCells)

bool FPCGExFindAllCellsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindAllCells)

	PCGEX_FWD(Artifacts)

	// Initialize Artifacts (output settings + OBB settings)
	if (!Context->Artifacts.Init(Context))
	{
		return false;
	}

	Context->HolesFacade = PCGExData::TryGetSingleFacade(Context, PCGExClusters::Labels::SourceHolesLabel, false, false);
	if (Context->HolesFacade && Settings->ProjectionDetails.Method == EPCGExProjectionMethod::Normal)
	{
		Context->Holes = MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), Settings->ProjectionDetails);
		Context->Holes->EnsureProjected(); // Project once upfront
	}

	// Initialize hole growth (will read per-point growth attribute if needed)
	PCGEX_FWD(HoleGrowth)
	if (Context->HolesFacade)
	{
		Context->HoleGrowth.Init(Context, Context->HolesFacade);
	}

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = PCGExCells::OutputLabels::Paths;

	Context->OutputCellBounds = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputCellBounds->OutputPin = PCGExCells::OutputLabels::CellBounds;

	return true;
}

bool FPCGExFindAllCellsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindAllCellsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindAllCells)
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


namespace PCGExFindAllCells
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindAllCells::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		if (Context->HolesFacade)
		{
			Holes = Context->Holes ? Context->Holes : MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), ProjectionDetails);
			if (Holes)
			{
				Holes->EnsureProjected();
			}
		}

		CellsConstraints = MakeShared<PCGExClusters::FCellConstraints>(Settings->Constraints);
		CellsConstraints->Reserve(Cluster->Edges->Num());
		CellsConstraints->Holes = Holes;

		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		// Enumerate all cells - get failed cells too if we need hole expansion
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCells;
		const bool bNeedFailedCells = Context->HoleGrowth.HasPotentialGrowth() && Holes;
		Enumerator->EnumerateAllFaces(ValidCells, CellsConstraints.ToSharedRef(), bNeedFailedCells ? &FailedCells : nullptr, Settings->Constraints.bOmitWrappingBounds);

		// Hole growth: a cell holding a hole already failed; this drops the cells around it too.
		if (bNeedFailedCells && !FailedCells.IsEmpty())
		{
			PCGExClusters::ExcludeHoleCells(
				ValidCells, FailedCells, *Holes, Context->HolesFacade.ToSharedRef(),
				Enumerator->GetOrBuildAdjacencyMap(Enumerator->GetWrapperFaceIndex()), Context->HoleGrowth);
		}

		// Merge adjacent valid cells into connected components when enabled
		if (Context->HoleGrowth.bMergeAdjacentCells && !ValidCells.IsEmpty())
		{
			const TSharedPtr<TArray<FVector2D>> ProjectedPositions = CellsConstraints->Enumerator ? CellsConstraints->Enumerator->GetProjectedPositions() : nullptr;

			TArray<TSharedPtr<PCGExClusters::FCell>> Merged = PCGExClusters::MergeAdjacentCells(
				ValidCells, CellsConstraints.ToSharedRef(), Cluster.Get(), ProjectedPositions);

			if (!Merged.IsEmpty())
			{
				ValidCells = MoveTemp(Merged);
			}
		}

		// The wrapper is only kept as the sole cell, and never goes through the merge.
		if (ValidCells.IsEmpty() && CellsConstraints->WrapperCell && Settings->Constraints.bKeepWrapperIfSolePath)
		{
			ValidCells.Add(CellsConstraints->WrapperCell);
		}

		const int32 NumCells = ValidCells.Num();
		if (NumCells == 0)
		{
			return true;
		}

		CellProcessor = MakeShared<PCGExClusters::FCellPathBuilder>();
		CellProcessor->Cluster = Cluster;
		CellProcessor->TaskManager = TaskManager;
		CellProcessor->Artifacts = &Context->Artifacts;
		CellProcessor->EdgeDataFacade = EdgeDataFacade;

		if (Settings->Artifacts.bOutputCellBounds)
		{
			if (!PCGExClusters::ProcessCellsAsOBBPoints(
				Cluster, ValidCells, Context->OutputCellBounds, VtxDataFacade->Source,
				PCGExData::FIOSortKey{EdgeDataFacade->Source->IOIndex}, Context->Artifacts, TaskManager))
			{
				return false;
			}
		}

		if (Settings->Artifacts.bOutputPaths)
		{
			CellsIO.SetNum(NumCells);
			if (!Context->OutputPaths->EmplaceBatch<UPCGPointArrayData>(CellsIO, VtxDataFacade->Source, PCGExData::EIOInit::New))
			{
				return false;
			}

			StartParallelLoopForRange(NumCells);
		}

		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			if (const TSharedPtr<PCGExData::FPointIO> IO = CellsIO[Index])
			{
				CellProcessor->ProcessCell(ValidCells[Index], IO, TEXT(""), Index);
			}
			ValidCells[Index] = nullptr;
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExFindAllCellsContext, UPCGExFindAllCellsSettings>::Cleanup();
		if (CellsConstraints)
		{
			CellsConstraints->Cleanup();
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
