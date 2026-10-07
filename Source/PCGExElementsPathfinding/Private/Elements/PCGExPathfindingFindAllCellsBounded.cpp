// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindAllCellsBounded.h"

#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Clusters/Artifacts/PCGExCell.h"
#include "Clusters/Artifacts/PCGExCellPathBuilder.h"
#include "Clusters/Artifacts/PCGExPlanarFaceEnumerator.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExFindAllCellsBounded"
#define PCGEX_NAMESPACE FindAllCellsBounded

TArray<FPCGPinProperties> UPCGExFindAllCellsBoundedSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExClusters::Labels::SourceHolesLabel, "Omit cells that contain any points from this dataset", Normal)
	PCGEX_PIN_SPATIAL(PCGExCellTriage::Labels::SourceBounds, "Spatial data whose bounds will be used to triage cells", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindAllCellsBoundedSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	PCGExCellTriage::DeclareOutputPins(PinProperties, OutputMode, TriageFlags, Artifacts.bOutputPaths, Artifacts.bOutputCellBounds);

	return PinProperties;
}

PCGExData::EIOInit UPCGExFindAllCellsBoundedSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFindAllCellsBoundedSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(FindAllCellsBounded)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FindAllCellsBounded)

bool FPCGExFindAllCellsBoundedElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindAllCellsBounded)

	PCGEX_FWD(Artifacts)

	// Initialize Artifacts (output settings + OBB settings)
	if (!Context->Artifacts.Init(Context))
	{
		return false;
	}

	if (!Context->Triage.Init(
		Context, PCGExCellTriage::Labels::SourceBounds, Settings->OutputMode, Settings->TriageFlags,
		Settings->Artifacts.bOutputPaths, Settings->Artifacts.bOutputCellBounds))
	{
		return false;
	}

	Context->HolesFacade = PCGExData::TryGetSingleFacade(Context, PCGExClusters::Labels::SourceHolesLabel, false, false);
	if (Context->HolesFacade && Settings->ProjectionDetails.Method == EPCGExProjectionMethod::Normal)
	{
		Context->Holes = MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->HolesFacade.ToSharedRef(), Settings->ProjectionDetails);
		Context->Holes->EnsureProjected();
	}

	// Initialize hole growth (will read per-point growth attribute if needed)
	PCGEX_FWD(HoleGrowth)
	if (Context->HolesFacade)
	{
		Context->HoleGrowth.Init(Context, Context->HolesFacade);
	}

	return true;
}

bool FPCGExFindAllCellsBoundedElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindAllCellsBoundedElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindAllCellsBounded)
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

	(void)Context->Triage.StageOutputs(Context->OutputData.InactiveOutputPinBitmask);

	return Context->TryComplete();
}


namespace PCGExFindAllCellsBounded
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindAllCellsBounded::Process);

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

		// Build enumerator and enumerate cells within bounds (early culling optimization)
		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		TArray<TSharedPtr<PCGExClusters::FCell>> AllCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCells;
		const bool bNeedOutside = Settings->OutputOutside();
		const bool bNeedFailedCells = Context->HoleGrowth.HasPotentialGrowth() && Holes;
		Enumerator->EnumerateFacesWithinBounds(
			AllCells,
			CellsConstraints.ToSharedRef(),
			Context->Triage.BoundsFilter,
			bNeedOutside, // Only include outside faces if user wants them
			bNeedFailedCells ? &FailedCells : nullptr,
			Settings->Constraints.bOmitWrappingBounds);

		// Hole growth: a cell holding a hole already failed; this drops the cells around it too.
		if (bNeedFailedCells && !FailedCells.IsEmpty())
		{
			PCGExClusters::ExcludeHoleCells(
				AllCells, FailedCells, *Holes, Context->HolesFacade.ToSharedRef(),
				Enumerator->GetOrBuildAdjacencyMap(Enumerator->GetWrapperFaceIndex()), Context->HoleGrowth);
		}

		// Merge adjacent valid cells into connected components when enabled
		if (Context->HoleGrowth.bMergeAdjacentCells && !AllCells.IsEmpty())
		{
			const TSharedPtr<TArray<FVector2D>> ProjectedPositions = CellsConstraints->Enumerator ? CellsConstraints->Enumerator->GetProjectedPositions() : nullptr;

			TArray<TSharedPtr<PCGExClusters::FCell>> Merged = PCGExClusters::MergeAdjacentCells(
				AllCells, CellsConstraints.ToSharedRef(), Cluster.Get(), ProjectedPositions);

			if (!Merged.IsEmpty())
			{
				AllCells = MoveTemp(Merged);
			}
		}

		// The wrapper is only kept as the sole cell, and never goes through the merge.
		if (AllCells.IsEmpty() && CellsConstraints->WrapperCell && Settings->Constraints.bKeepWrapperIfSolePath)
		{
			AllCells.Add(CellsConstraints->WrapperCell);
		}

		// Classify the cells; a category that is not wanted drops its cells.
		Buckets.Add(AllCells, Context->Triage);

		if (Buckets.NumCells() == 0)
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
			if (!Buckets.EmitBounds(
				Cluster, VtxDataFacade->Source, PCGExData::FIOSortKey{EdgeDataFacade->Source->IOIndex},
				Context->Triage, Context->Artifacts, TaskManager))
			{
				return false;
			}
		}

		if (Settings->Artifacts.bOutputPaths)
		{
			if (!Buckets.PreparePaths(VtxDataFacade->Source, Context->Triage))
			{
				return false;
			}

			const int32 NumPaths = Buckets.NumPaths();
			if (NumPaths > 0)
			{
				StartParallelLoopForRange(NumPaths);
			}
		}

		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			EPCGExCellTriageResult Category = EPCGExCellTriageResult::Inside;
			int32 CellIndex = INDEX_NONE;
			Buckets.Resolve(Index, Category, CellIndex);

			const int32 CategoryIndex = PCGExCellTriage::GetCategoryIndex(Category);
			TSharedPtr<PCGExClusters::FCell>& Cell = Buckets.Cells[CategoryIndex][CellIndex];

			if (const TSharedPtr<PCGExData::FPointIO>& IO = Buckets.PathIOs[CategoryIndex][CellIndex])
			{
				CellProcessor->ProcessCell(Cell, IO, Context->Triage.GetTag(Category), Index);
			}

			Cell = nullptr;
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExFindAllCellsBoundedContext, UPCGExFindAllCellsBoundedSettings>::Cleanup();
		if (CellsConstraints)
		{
			CellsConstraints->Cleanup();
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
