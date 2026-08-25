// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindAllCellsBounded.h"

#include "Containers/Queue.h"
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
	PCGEX_PIN_SPATIAL(PCGExFindAllCellsBounded::SourceBoundsLabel, "Spatial data whose bounds will be used to triage cells", Required)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindAllCellsBoundedSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (OutputMode == EPCGExCellTriageOutput::Separate)
	{
		// Separate pins for each triage category - all pins present, unwanted ones marked Advanced
		if (Artifacts.bOutputPaths)
		{
			if (OutputInside())
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputPathsInsideLabel, "Cell paths fully inside bounds", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputPathsInsideLabel, "Cell paths fully inside bounds", Advanced)
			}

			if (OutputTouching())
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputPathsTouchingLabel, "Cell paths touching bounds", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputPathsTouchingLabel, "Cell paths touching bounds", Advanced)
			}

			if (OutputOutside())
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputPathsOutsideLabel, "Cell paths outside bounds", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputPathsOutsideLabel, "Cell paths outside bounds", Advanced)
			}
		}

		if (Artifacts.bOutputCellBounds)
		{
			if (OutputInside())
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputBoundsInsideLabel, "Cell OBB bounds fully inside", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputBoundsInsideLabel, "Cell OBB bounds fully inside", Advanced)
			}

			if (OutputTouching())
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputBoundsTouchingLabel, "Cell OBB bounds touching", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputBoundsTouchingLabel, "Cell OBB bounds touching", Advanced)
			}

			if (OutputOutside())
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputBoundsOutsideLabel, "Cell OBB bounds outside", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindAllCellsBounded::OutputBoundsOutsideLabel, "Cell OBB bounds outside", Advanced)
			}
		}
	}
	else
	{
		// Combined output with triage tags
		if (Artifacts.bOutputPaths)
		{
			PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cell contours as closed paths (tagged with triage result)", Required)
		}
		if (Artifacts.bOutputCellBounds)
		{
			PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cell OBB bounds as points (tagged with triage result)", Required)
		}
	}

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

	// Get required bounds from spatial input
	TArray<FPCGTaggedData> BoundsData = Context->InputData.GetSpatialInputsByPin(PCGExFindAllCellsBounded::SourceBoundsLabel);
	if (BoundsData.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Missing required Bounds input."));
		return false;
	}

	if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(BoundsData[0].Data))
	{
		Context->BoundsFilter = SpatialData->GetBounds();
	}
	else
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Invalid Bounds input - must be spatial data."));
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

	// Initialize output collections based on mode
	if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
	{
		// All pins are always present, create all collections (empty ones produce empty outputs)
		if (Settings->Artifacts.bOutputPaths)
		{
			Context->OutputPathsInside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsInside->OutputPin = PCGExFindAllCellsBounded::OutputPathsInsideLabel;

			Context->OutputPathsTouching = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsTouching->OutputPin = PCGExFindAllCellsBounded::OutputPathsTouchingLabel;

			Context->OutputPathsOutside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsOutside->OutputPin = PCGExFindAllCellsBounded::OutputPathsOutsideLabel;
		}

		if (Settings->Artifacts.bOutputCellBounds)
		{
			Context->OutputCellBoundsInside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsInside->OutputPin = PCGExFindAllCellsBounded::OutputBoundsInsideLabel;

			Context->OutputCellBoundsTouching = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsTouching->OutputPin = PCGExFindAllCellsBounded::OutputBoundsTouchingLabel;

			Context->OutputCellBoundsOutside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsOutside->OutputPin = PCGExFindAllCellsBounded::OutputBoundsOutsideLabel;
		}
	}
	else
	{
		// Combined mode - single collection for all categories
		if (Settings->Artifacts.bOutputPaths)
		{
			Context->OutputPathsInside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsInside->OutputPin = PCGExCells::OutputLabels::Paths;
		}

		if (Settings->Artifacts.bOutputCellBounds)
		{
			Context->OutputCellBoundsInside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsInside->OutputPin = PCGExCells::OutputLabels::CellBounds;
		}
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

	uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;
	int32 PinIndex = 0;

	if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
	{
		// All pins are always present in fixed order: PathsInside, PathsTouching, PathsOutside, BoundsInside, BoundsTouching, BoundsOutside
		// Mark as inactive if collection is empty or disabled
		if (Settings->Artifacts.bOutputPaths)
		{
			if (!Context->OutputPathsInside || !Context->OutputPathsInside->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
			if (!Context->OutputPathsTouching || !Context->OutputPathsTouching->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
			if (!Context->OutputPathsOutside || !Context->OutputPathsOutside->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
		}
		if (Settings->Artifacts.bOutputCellBounds)
		{
			if (!Context->OutputCellBoundsInside || !Context->OutputCellBoundsInside->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
			if (!Context->OutputCellBoundsTouching || !Context->OutputCellBoundsTouching->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
			if (!Context->OutputCellBoundsOutside || !Context->OutputCellBoundsOutside->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
		}
	}
	else
	{
		// Combined mode
		if (Settings->Artifacts.bOutputPaths)
		{
			if (!Context->OutputPathsInside || !Context->OutputPathsInside->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
		}
		if (Settings->Artifacts.bOutputCellBounds)
		{
			if (!Context->OutputCellBoundsInside || !Context->OutputCellBoundsInside->StageOutputs())
			{
				Mask |= 1ULL << PinIndex;
			}
			PinIndex++;
		}
	}

	return Context->TryComplete();
}


namespace PCGExFindAllCellsBounded
{
	FProcessor::~FProcessor()
	{
	}

	ECellTriageResult FProcessor::ClassifyCell(const TSharedPtr<PCGExClusters::FCell>& InCell) const
	{
		return PCGExCellTriage::ClassifyCell(InCell->Data.Bounds, InCell->Data.Centroid, Context->BoundsFilter);
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

		// Set up cell constraints
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
			Context->BoundsFilter,
			bNeedOutside, // Only include outside faces if user wants them
			bNeedFailedCells ? &FailedCells : nullptr,
			Settings->Constraints.bOmitWrappingBounds);

		// Process hole growth expansion if enabled
		if (bNeedFailedCells && !FailedCells.IsEmpty())
		{
			// Build adjacency map
			const int32 WrapperFaceIndex = Enumerator->GetWrapperFaceIndex();
			CellAdjacencyMap = Enumerator->GetOrBuildAdjacencyMap(WrapperFaceIndex);

			// Find cells that failed due to holes and expand exclusion
			const int32 NumHoles = Context->HolesFacade->GetNum();
			const TConstPCGValueRange<FTransform> HoleTransforms = Context->HolesFacade->GetIn()->GetConstTransformValueRange();
			for (const TSharedPtr<PCGExClusters::FCell>& FailedCell : FailedCells)
			{
				if (!FailedCell || FailedCell->Polygon.IsEmpty() || FailedCell->FaceIndex < 0)
				{
					continue;
				}

				// Check if this cell contains a hole (per-face frame in LocalTangent -- see FCell::ContainsPoint)
				bool bContainsHole = false;
				int32 HoleIndex = -1;
				for (int32 i = 0; i < NumHoles && !bContainsHole; ++i)
				{
					if (FailedCell->ContainsPoint(Holes->GetProjected(i), HoleTransforms[i].GetLocation()))
					{
						bContainsHole = true;
						HoleIndex = i;
					}
				}

				if (bContainsHole && HoleIndex >= 0)
				{
					// Mark this cell for exclusion
					ExcludedFaceIndices.Add(FailedCell->FaceIndex);

					// Expand to adjacent cells
					const int32 Growth = Context->HoleGrowth.GetGrowth(HoleIndex);
					if (Growth > 0)
					{
						ExpandHoleExclusion(HoleIndex, FailedCell->FaceIndex, Growth);
					}
				}
			}

			// Remove excluded cells from AllCells
			if (!ExcludedFaceIndices.IsEmpty())
			{
				AllCells.RemoveAll([this](const TSharedPtr<PCGExClusters::FCell>& Cell)
				{
					return Cell && Cell->FaceIndex >= 0 && ExcludedFaceIndices.Contains(Cell->FaceIndex);
				});
			}
		}

		// Handle wrapper cell if applicable
		if (AllCells.IsEmpty() && CellsConstraints->WrapperCell && Settings->Constraints.bKeepWrapperIfSolePath)
		{
			AllCells.Add(CellsConstraints->WrapperCell);
		}

		if (AllCells.IsEmpty())
		{
			return true;
		}

		// Initialize cell processor
		CellProcessor = MakeShared<PCGExClusters::FCellPathBuilder>();
		CellProcessor->Cluster = Cluster;
		CellProcessor->TaskManager = TaskManager;
		CellProcessor->Artifacts = &Context->Artifacts;
		CellProcessor->EdgeDataFacade = EdgeDataFacade;

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

		// Classify cells by bounds relationship, respecting enable flags
		for (const TSharedPtr<PCGExClusters::FCell>& Cell : AllCells)
		{
			if (!Cell)
			{
				continue;
			}

			const ECellTriageResult Result = ClassifyCell(Cell);

			switch (Result)
			{
			case ECellTriageResult::Inside:
				if (Settings->OutputInside())
				{
					CellsInside.Add(Cell);
				}
				break;
			case ECellTriageResult::Touching:
				if (Settings->OutputTouching())
				{
					CellsTouching.Add(Cell);
				}
				break;
			case ECellTriageResult::Outside:
				if (Settings->OutputOutside())
				{
					CellsOutside.Add(Cell);
				}
				break;
			}
		}

		// Output CellBounds for each category
		auto OutputCellBounds = [&](const TArray<TSharedPtr<PCGExClusters::FCell>>& Cells, const TSharedPtr<PCGExData::FPointIOCollection>& OutputCollection, const FString& TriageTag = TEXT(""))
		{
			if (!OutputCollection || Cells.IsEmpty())
			{
				return;
			}

			TSharedPtr<PCGExData::FPointIO> OBBPointIO = OutputCollection->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
			if (!OBBPointIO)
			{
				return;
			}

			if (!TriageTag.IsEmpty())
			{
				OBBPointIO->Tags->AddRaw(TriageTag);
			}
			OBBPointIO->IOIndex = EdgeDataFacade->Source->IOIndex;
			PCGExClusters::Helpers::CleanupClusterData(OBBPointIO);

			PCGEX_MAKE_SHARED(OBBFacade, PCGExData::FFacade, OBBPointIO.ToSharedRef())
			PCGExClusters::ProcessCellsAsOBBPoints(Cluster, Cells, OBBFacade, Context->Artifacts, TaskManager);
		};

		if (Settings->Artifacts.bOutputCellBounds)
		{
			if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
			{
				OutputCellBounds(CellsInside, Context->OutputCellBoundsInside);
				OutputCellBounds(CellsTouching, Context->OutputCellBoundsTouching);
				OutputCellBounds(CellsOutside, Context->OutputCellBoundsOutside);
			}
			else
			{
				OutputCellBounds(CellsInside, Context->OutputCellBoundsInside, PCGExCellTriage::TagInside);
				OutputCellBounds(CellsTouching, Context->OutputCellBoundsInside, PCGExCellTriage::TagTouching);
				OutputCellBounds(CellsOutside, Context->OutputCellBoundsInside, PCGExCellTriage::TagOutside);
			}
		}

		// Prepare path outputs
		auto PreparePathOutputs = [&](TArray<TSharedPtr<PCGExClusters::FCell>>& Cells, TArray<TSharedPtr<PCGExData::FPointIO>>& CellsIO, TArray<FString>& CellTags, const TSharedPtr<PCGExData::FPointIOCollection>& OutputCollection, const FString& TriageTag)
		{
			if (!OutputCollection || Cells.IsEmpty())
			{
				return;
			}

			CellsIO.SetNum(Cells.Num());
			CellTags.Init(TriageTag, Cells.Num());
			if (!OutputCollection->EmplaceBatch(CellsIO, VtxDataFacade->Source, PCGExData::EIOInit::New))
			{
				return;
			}
		};

		if (Settings->Artifacts.bOutputPaths)
		{
			if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
			{
				PreparePathOutputs(CellsInside, CellsIOInside, CellTagsInside, Context->OutputPathsInside, TEXT(""));
				PreparePathOutputs(CellsTouching, CellsIOTouching, CellTagsTouching, Context->OutputPathsTouching, TEXT(""));
				PreparePathOutputs(CellsOutside, CellsIOOutside, CellTagsOutside, Context->OutputPathsOutside, TEXT(""));
			}
			else
			{
				PreparePathOutputs(CellsInside, CellsIOInside, CellTagsInside, Context->OutputPathsInside, PCGExCellTriage::TagInside);
				PreparePathOutputs(CellsTouching, CellsIOTouching, CellTagsTouching, Context->OutputPathsInside, PCGExCellTriage::TagTouching);
				PreparePathOutputs(CellsOutside, CellsIOOutside, CellTagsOutside, Context->OutputPathsInside, PCGExCellTriage::TagOutside);
			}

			const int32 TotalCells = CellsIOInside.Num() + CellsIOTouching.Num() + CellsIOOutside.Num();
			if (TotalCells > 0)
			{
				StartParallelLoopForRange(TotalCells);
			}
		}

		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const int32 InsideCount = CellsIOInside.Num();
		const int32 TouchingCount = CellsIOTouching.Num();

		PCGEX_SCOPE_LOOP(Index)
		{
			if (Index < InsideCount)
			{
				if (const TSharedPtr<PCGExData::FPointIO> IO = CellsIOInside[Index])
				{
					CellProcessor->ProcessCell(CellsInside[Index], IO, CellTagsInside.IsValidIndex(Index) ? CellTagsInside[Index] : TEXT(""));
				}
				CellsInside[Index] = nullptr;
			}
			else if (Index < InsideCount + TouchingCount)
			{
				const int32 LocalIndex = Index - InsideCount;
				if (const TSharedPtr<PCGExData::FPointIO> IO = CellsIOTouching[LocalIndex])
				{
					CellProcessor->ProcessCell(CellsTouching[LocalIndex], IO, CellTagsTouching.IsValidIndex(LocalIndex) ? CellTagsTouching[LocalIndex] : TEXT(""));
				}
				CellsTouching[LocalIndex] = nullptr;
			}
			else
			{
				const int32 LocalIndex = Index - InsideCount - TouchingCount;
				if (const TSharedPtr<PCGExData::FPointIO> IO = CellsIOOutside[LocalIndex])
				{
					CellProcessor->ProcessCell(CellsOutside[LocalIndex], IO, CellTagsOutside.IsValidIndex(LocalIndex) ? CellTagsOutside[LocalIndex] : TEXT(""));
				}
				CellsOutside[LocalIndex] = nullptr;
			}
		}
	}

	void FProcessor::ExpandHoleExclusion(int32 HoleIndex, int32 InitialFaceIndex, int32 MaxGrowth)
	{
		if (MaxGrowth <= 0)
		{
			return;
		}
		if (CellAdjacencyMap.IsEmpty())
		{
			return;
		}

		TSet<int32> Visited;
		Visited.Add(InitialFaceIndex); // Don't re-visit the initial cell

		TQueue<TPair<int32, int32>> Queue; // FaceIndex, CurrentDepth

		// Start with immediate neighbors (depth 1)
		if (const TSet<int32>* Adjacent = CellAdjacencyMap.Find(InitialFaceIndex))
		{
			for (int32 AdjFace : *Adjacent)
			{
				if (AdjFace >= 0 && !Visited.Contains(AdjFace))
				{
					Queue.Enqueue({AdjFace, 1});
					Visited.Add(AdjFace);
				}
			}
		}

		while (!Queue.IsEmpty())
		{
			TPair<int32, int32> Current;
			Queue.Dequeue(Current);
			const int32 FaceIndex = Current.Key;
			const int32 Depth = Current.Value;

			// Mark this face for exclusion
			ExcludedFaceIndices.Add(FaceIndex);

			// Continue BFS if not at max depth
			if (Depth < MaxGrowth)
			{
				if (const TSet<int32>* Adjacent = CellAdjacencyMap.Find(FaceIndex))
				{
					for (int32 AdjFace : *Adjacent)
					{
						if (AdjFace >= 0 && !Visited.Contains(AdjFace))
						{
							Queue.Enqueue({AdjFace, Depth + 1});
							Visited.Add(AdjFace);
						}
					}
				}
			}
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
