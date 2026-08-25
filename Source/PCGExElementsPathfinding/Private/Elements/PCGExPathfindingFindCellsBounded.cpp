// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindCellsBounded.h"

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
#include "Data/Utils/PCGExDataForward.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExFindContoursBounded"
#define PCGEX_NAMESPACE FindContoursBounded

TArray<FPCGPinProperties> UPCGExFindContoursBoundedSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seeds associated with the main input points", Required)
	PCGEX_PIN_SPATIAL(PCGExFindContoursBounded::SourceBoundsLabel, "Spatial data whose bounds will be used to triage cells", Required)
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SeedOwnership == EPCGExCellSeedOwnership::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);
	return PinProperties;
}

bool UPCGExFindContoursBoundedSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return SeedOwnership == EPCGExCellSeedOwnership::BestCandidate;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExFindContoursBoundedSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (OutputMode == EPCGExCellTriageOutput::Separate)
	{
		// All pins present, unwanted ones marked Advanced
		if (Artifacts.bOutputPaths)
		{
			if (OutputInside())
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputPathsInsideLabel, "Cell paths fully inside bounds", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputPathsInsideLabel, "Cell paths fully inside bounds", Advanced)
			}

			if (OutputTouching())
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputPathsTouchingLabel, "Cell paths touching bounds", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputPathsTouchingLabel, "Cell paths touching bounds", Advanced)
			}

			if (OutputOutside())
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputPathsOutsideLabel, "Cell paths outside bounds", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputPathsOutsideLabel, "Cell paths outside bounds", Advanced)
			}
		}

		if (Artifacts.bOutputCellBounds)
		{
			if (OutputInside())
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputBoundsInsideLabel, "Cell OBB bounds fully inside", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputBoundsInsideLabel, "Cell OBB bounds fully inside", Advanced)
			}

			if (OutputTouching())
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputBoundsTouchingLabel, "Cell OBB bounds touching", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputBoundsTouchingLabel, "Cell OBB bounds touching", Advanced)
			}

			if (OutputOutside())
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputBoundsOutsideLabel, "Cell OBB bounds outside", Normal)
			}
			else
			{
				PCGEX_PIN_POINTS(PCGExFindContoursBounded::OutputBoundsOutsideLabel, "Cell OBB bounds outside", Advanced)
			}
		}
	}
	else
	{
		if (Artifacts.bOutputPaths)
		{
			PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cell contours as closed paths (tagged with triage result)", Required)
		}
		if (Artifacts.bOutputCellBounds)
		{
			PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cell OBB bounds as points (tagged with triage result)", Required)
		}
	}

	if (bOutputFilteredSeeds)
	{
		PCGEX_PIN_POINT(PCGExFindContoursBounded::OutputGoodSeedsLabel, "GoodSeeds", Required)
		PCGEX_PIN_POINT(PCGExFindContoursBounded::OutputBadSeedsLabel, "BadSeeds", Required)
	}

	return PinProperties;
}

PCGExData::EIOInit UPCGExFindContoursBoundedSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFindContoursBoundedSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(FindContoursBounded)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FindContoursBounded)

bool FPCGExFindContoursBoundedElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindContoursBounded)

	PCGEX_FWD(Artifacts)

	if (!Context->Artifacts.Init(Context))
	{
		return false;
	}

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	PCGEX_FWD(SeedGrowth)
	Context->SeedGrowth.Init(Context, Context->SeedsDataFacade);

	// Initialize seed ownership handler
	Context->SeedOwnership = MakeShared<PCGExCells::FSeedOwnershipHandler>();
	Context->SeedOwnership->Method = Settings->SeedOwnership;
	Context->SeedOwnership->SortDirection = Settings->SortDirection;
	if (!Context->SeedOwnership->Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}

	// Get required bounds
	TArray<FPCGTaggedData> BoundsData = Context->InputData.GetSpatialInputsByPin(PCGExFindContoursBounded::SourceBoundsLabel);
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

	PCGEX_FWD(SeedAttributesToPathTags)
	if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}
	Context->SeedForwardHandler = Settings->SeedForwarding.GetHandler(Context->SeedsDataFacade);

	// Initialize output collections based on mode
	if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
	{
		// All pins are always present, create all collections (empty ones produce empty outputs)
		if (Settings->Artifacts.bOutputPaths)
		{
			Context->OutputPathsInside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsInside->OutputPin = PCGExFindContoursBounded::OutputPathsInsideLabel;

			Context->OutputPathsTouching = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsTouching->OutputPin = PCGExFindContoursBounded::OutputPathsTouchingLabel;

			Context->OutputPathsOutside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputPathsOutside->OutputPin = PCGExFindContoursBounded::OutputPathsOutsideLabel;
		}

		if (Settings->Artifacts.bOutputCellBounds)
		{
			Context->OutputCellBoundsInside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsInside->OutputPin = PCGExFindContoursBounded::OutputBoundsInsideLabel;

			Context->OutputCellBoundsTouching = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsTouching->OutputPin = PCGExFindContoursBounded::OutputBoundsTouchingLabel;

			Context->OutputCellBoundsOutside = MakeShared<PCGExData::FPointIOCollection>(Context);
			Context->OutputCellBoundsOutside->OutputPin = PCGExFindContoursBounded::OutputBoundsOutsideLabel;
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

	if (Settings->bOutputFilteredSeeds)
	{
		const int32 NumSeeds = Context->SeedsDataFacade->GetNum();

		Context->SeedQuality.Init(false, NumSeeds);

		Context->GoodSeeds = NewPointIO(Context->SeedsDataFacade->Source, PCGExFindContoursBounded::OutputGoodSeedsLabel);
		Context->GoodSeeds->InitializeOutput(PCGExData::EIOInit::Duplicate);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Context->GoodSeeds->GetOut(), NumSeeds);

		Context->BadSeeds = NewPointIO(Context->SeedsDataFacade->Source, PCGExFindContoursBounded::OutputBadSeedsLabel);
		Context->BadSeeds->InitializeOutput(PCGExData::EIOInit::Duplicate);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Context->BadSeeds->GetOut(), NumSeeds);
	}

	return true;
}

bool FPCGExFindContoursBoundedElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindContoursBoundedElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindContoursBounded)
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

	if (Settings->bOutputFilteredSeeds)
	{
		(void)Context->GoodSeeds->Gather(Context->SeedQuality);
		(void)Context->BadSeeds->Gather(Context->SeedQuality, true);

		(void)Context->GoodSeeds->StageOutput(Context);
		(void)Context->BadSeeds->StageOutput(Context);
	}

	return Context->TryComplete();
}


namespace PCGExFindContoursBounded
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
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindContoursBounded::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const int32 NumSeeds = Context->SeedsDataFacade->Source->GetNum();

		// Initialize cell processor
		CellProcessor = MakeShared<PCGExClusters::FCellPathBuilder>();
		CellProcessor->Cluster = Cluster;
		CellProcessor->TaskManager = TaskManager;
		CellProcessor->Artifacts = &Context->Artifacts;
		CellProcessor->BatchIndex = BatchIndex;
		CellProcessor->SeedsDataFacade = Context->SeedsDataFacade;
		CellProcessor->SeedAttributesToPathTags = &Context->SeedAttributesToPathTags;
		CellProcessor->SeedForwardHandler = Context->SeedForwardHandler;

		if (Settings->bOutputFilteredSeeds)
		{
			CellProcessor->SeedQuality = &Context->SeedQuality;
			CellProcessor->GoodSeeds = Context->GoodSeeds;
			CellProcessor->SeedMutations = &Settings->SeedMutations;
		}

		CellsConstraints = MakeShared<PCGExClusters::FCellConstraints>(Settings->Constraints);
		CellsConstraints->Reserve(Cluster->Edges->Num());
		// Face planes cost a fit per cell; only pay when the distance gate reads them (LocalTangent
		// always builds its own -- the polygon lives in that frame).
		CellsConstraints->bComputeFacePlanes = Settings->SeedPicking.MaxDistance > 0;

		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		TArray<TSharedPtr<PCGExClusters::FCell>> AllCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCells;
		const bool bNeedOutside = Settings->OutputOutside();
		Enumerator->EnumerateFacesWithinBounds(
			AllCells,
			CellsConstraints.ToSharedRef(),
			Context->BoundsFilter,
			bNeedOutside, // Only include outside faces if user wants them
			&FailedCells,
			true);
		WrapperCell = CellsConstraints->WrapperCell;

		Seeds = MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->SeedsDataFacade.ToSharedRef(), ProjectionDetails);
		Seeds->EnsureProjected();

		AllCellsIncludingFailed = AllCells;
		AllCellsIncludingFailed.Append(FailedCells);

		// Build adjacency map if growth is enabled
		if (Context->SeedGrowth.HasPotentialGrowth())
		{
			const int32 WrapperFaceIndex = Enumerator->GetWrapperFaceIndex();
			CellAdjacencyMap = Enumerator->GetOrBuildAdjacencyMap(WrapperFaceIndex);

			// Build FaceIndex -> Cell map for all cells (valid + failed)
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : AllCellsIncludingFailed)
			{
				if (Cell && Cell->FaceIndex >= 0)
				{
					FaceIndexToCellMap.Add(Cell->FaceIndex, Cell);
				}
			}
		}

		if (AllCells.IsEmpty() && WrapperCell)
		{
			HandleWrapperOnlyCase(NumSeeds);
			return true;
		}

		EnumeratedCells = MoveTemp(AllCells);

		// LocalTangent: arbitrate seed claims up front -- a seed sandwiched between STACKED parallel
		// cells projects inside both, and only the nearest plane may own it.
		if (Enumerator->IsLocalTangent())
		{
			TConstPCGValueRange<FTransform> SeedTransforms = Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
			const double MaxPlaneDistSq = Settings->SeedPicking.MaxDistance > 0 ? FMath::Square(Settings->SeedPicking.MaxDistance) : -1.0;

			SeedBestFace.Init(INDEX_NONE, NumSeeds);
			FailedCellsOnly = FailedCells;

			// Parallel when the pair count is heavy, or earlier when this is the only cluster in flight
			// (nested parallelism then competes with nothing -- bSoloClusterWorkload).
			const int64 PairOps = static_cast<int64>(NumSeeds) * EnumeratedCells.Num();
			const int32 NestedParallelThreshold = (bSoloClusterWorkload && PairOps > 4096) || PairOps > 65536 ? 32 : MAX_int32;

			PCGExMT::ParallelOrSequential(
				NumSeeds, [&](const int32 SeedIdx)
				{
					const FVector SeedPos = SeedTransforms[SeedIdx].GetLocation();
					const FVector2D SeedProjected = Seeds->GetProjected(SeedIdx);
					double BestDistSq = TNumericLimits<double>::Max();
					for (const TSharedPtr<PCGExClusters::FCell>& Cell : EnumeratedCells)
					{
						double DistSq = 0;
						if (!Cell || !Cell->ContainsPoint(SeedProjected, SeedPos, MaxPlaneDistSq, &DistSq))
						{
							continue;
						}
						if (DistSq < BestDistSq)
						{
							BestDistSq = DistSq;
							SeedBestFace[SeedIdx] = Cell->FaceIndex;
						}
					}
				}, NestedParallelThreshold);

			// Inverse map, so ProcessRange resolves each cell's claimants with one lookup.
			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
			{
				if (SeedBestFace[SeedIdx] != INDEX_NONE)
				{
					SeedFaceClaims.FindOrAdd(SeedBestFace[SeedIdx]).Add(SeedIdx);
				}
			}
		}

		StartParallelLoopForRange(EnumeratedCells.Num(), 64);

		return true;
	}

	void FProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
		ScopedValidCells = MakeShared<PCGExMT::TScopedArray<TSharedPtr<PCGExClusters::FCell>>>(Loops);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const int32 NumSeeds = Seeds->Num();
		const TSharedPtr<PCGExCells::FSeedOwnershipHandler>& SeedOwnership = Context->SeedOwnership;
		const bool bNeedsAllCandidates = SeedOwnership->NeedsAllCandidates();

		// LocalTangent cells re-project each seed into their own face frame, optionally gated on distance
		// to the face plane (Seed Picking's MaxDistance); planar cells test in the shared projection.
		TConstPCGValueRange<FTransform> SeedTransforms = Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
		const double MaxPlaneDistSq = Settings->SeedPicking.MaxDistance > 0 ? FMath::Square(Settings->SeedPicking.MaxDistance) : -1.0;

		TArray<TSharedPtr<PCGExClusters::FCell>>& CellsContainer = ScopedValidCells->Get_Ref(Scope);
		CellsContainer.Reserve(Scope.Count);

		TArray<int32> CandidateSeeds; // Reused per cell
		CandidateSeeds.Reserve(8);

		PCGEX_SCOPE_LOOP(CellIndex)
		{
			const TSharedPtr<PCGExClusters::FCell>& Cell = EnumeratedCells[CellIndex];
			if (!Cell || Cell->Polygon.IsEmpty())
			{
				continue;
			}

			CandidateSeeds.Reset();

			if (!SeedBestFace.IsEmpty())
			{
				// Arbitrated (LocalTangent): the inverse map already holds this cell's claimants.
				if (const TArray<int32>* Claims = SeedFaceClaims.Find(Cell->FaceIndex))
				{
					CandidateSeeds = *Claims;
				}
			}
			else
			{
				// Find all seeds inside this cell
				for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
				{
					if (Cell->ContainsPoint(Seeds->GetProjected(SeedIdx), SeedTransforms[SeedIdx].GetLocation(), MaxPlaneDistSq))
					{
						CandidateSeeds.Add(SeedIdx);

						// For SeedOrder mode, first match wins - break early
						if (!bNeedsAllCandidates)
						{
							break;
						}
					}
				}
			}

			// Only output cells that contain at least one seed
			if (!CandidateSeeds.IsEmpty())
			{
				const int32 WinnerSeedIndex = SeedOwnership->PickWinner(CandidateSeeds, Cell->Data.Centroid);
				Cell->CustomIndex = WinnerSeedIndex;
				CellsContainer.Add(Cell);
			}
		}
	}

	void FProcessor::HandleWrapperOnlyCase(const int32 NumSeeds)
	{
		if (!WrapperCell)
		{
			return;
		}

		// Classify wrapper cell
		const ECellTriageResult WrapperResult = ClassifyCell(WrapperCell);

		// Check if this category is enabled
		bool bCategoryEnabled = false;
		switch (WrapperResult)
		{
		case ECellTriageResult::Inside:
			bCategoryEnabled = Settings->OutputInside();
			break;
		case ECellTriageResult::Touching:
			bCategoryEnabled = Settings->OutputTouching();
			break;
		case ECellTriageResult::Outside:
			bCategoryEnabled = Settings->OutputOutside();
			break;
		}

		if (!bCategoryEnabled)
		{
			return;
		}

		const TSharedPtr<PCGExCells::FSeedOwnershipHandler>& SeedOwnership = Context->SeedOwnership;
		TArray<int32> CandidateSeeds;
		CandidateSeeds.Reserve(NumSeeds);

		TConstPCGValueRange<FTransform> SeedTransforms = Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
		const double MaxPlaneDistSq = Settings->SeedPicking.MaxDistance > 0 ? FMath::Square(Settings->SeedPicking.MaxDistance) : -1.0;

		for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
		{
			bool bConsumed = false;
			const FVector2D& SeedPoint = Seeds->GetProjected(SeedIdx);

			for (const TSharedPtr<PCGExClusters::FCell>& Cell : AllCellsIncludingFailed)
			{
				if (Cell && Cell->ContainsPoint(SeedPoint, SeedTransforms[SeedIdx].GetLocation(), MaxPlaneDistSq))
				{
					bConsumed = true;
					break;
				}
			}

			if (bConsumed)
			{
				continue;
			}

			const FVector& SeedPos = SeedTransforms[SeedIdx].GetLocation();
			double ClosestEdgeDistSq = TNumericLimits<double>::Max();

			Cluster->GetEdgeOctree()->FindNearbyElements(SeedPos, [&](const PCGExOctree::FItem& Item)
			{
				const double DistSq = Cluster->GetPointDistToEdgeSquared(Item.Index, SeedPos);
				if (DistSq < ClosestEdgeDistSq)
				{
					ClosestEdgeDistSq = DistSq;
				}
			});

			if (Settings->SeedPicking.WithinDistanceSquared(ClosestEdgeDistSq))
			{
				CandidateSeeds.Add(SeedIdx);
			}
		}

		// Pick winner using seed ownership handler
		const int32 BestSeedIdx = SeedOwnership->PickWinner(CandidateSeeds, WrapperCell->Data.Centroid);

		if (BestSeedIdx == INDEX_NONE)
		{
			return;
		}

		WrapperCell->CustomIndex = BestSeedIdx;

		TArray<TSharedPtr<PCGExClusters::FCell>> WrapperArray;
		WrapperArray.Add(WrapperCell);

		// Determine output collection based on triage result
		TSharedPtr<PCGExData::FPointIOCollection> PathCollection;
		TSharedPtr<PCGExData::FPointIOCollection> BoundsCollection;

		if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
		{
			switch (WrapperResult)
			{
			case ECellTriageResult::Inside:
				PathCollection = Context->OutputPathsInside;
				BoundsCollection = Context->OutputCellBoundsInside;
				break;
			case ECellTriageResult::Touching:
				PathCollection = Context->OutputPathsTouching;
				BoundsCollection = Context->OutputCellBoundsTouching;
				break;
			case ECellTriageResult::Outside:
				PathCollection = Context->OutputPathsOutside;
				BoundsCollection = Context->OutputCellBoundsOutside;
				break;
			}
		}
		else
		{
			PathCollection = Context->OutputPathsInside;
			BoundsCollection = Context->OutputCellBoundsInside;
		}

		// Get tag for Combined mode
		FString TriageTag;
		if (Settings->OutputMode == EPCGExCellTriageOutput::Combined)
		{
			switch (WrapperResult)
			{
			case ECellTriageResult::Inside:
				TriageTag = PCGExCellTriage::TagInside;
				break;
			case ECellTriageResult::Touching:
				TriageTag = PCGExCellTriage::TagTouching;
				break;
			case ECellTriageResult::Outside:
				TriageTag = PCGExCellTriage::TagOutside;
				break;
			}
		}

		if (Settings->Artifacts.bOutputCellBounds && BoundsCollection)
		{
			TSharedPtr<PCGExData::FPointIO> OBBPointIO = BoundsCollection->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
			if (!OBBPointIO)
			{
				return;
			}

			if (!TriageTag.IsEmpty())
			{
				OBBPointIO->Tags->AddRaw(TriageTag);
			}
			OBBPointIO->IOIndex = BatchIndex;
			PCGExClusters::Helpers::CleanupClusterData(OBBPointIO);

			PCGEX_MAKE_SHARED(OBBFacade, PCGExData::FFacade, OBBPointIO.ToSharedRef())
			PCGExClusters::ProcessCellsAsOBBPoints(Cluster, WrapperArray, OBBFacade, Context->Artifacts, TaskManager);
		}

		if (Settings->Artifacts.bOutputPaths && PathCollection)
		{
			CellProcessor->ProcessSeededCell(WrapperCell, PathCollection->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New), TriageTag);
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		TArray<TSharedPtr<PCGExClusters::FCell>> ValidCells;
		ScopedValidCells->Collapse(ValidCells);

		// Process seed growth expansion if enabled
		if (Context->SeedGrowth.HasPotentialGrowth() && !CellAdjacencyMap.IsEmpty())
		{
			// Record initial seed matches (depth 0) and perform expansion
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
			{
				if (!Cell || Cell->FaceIndex < 0)
				{
					continue;
				}

				const int32 SeedIndex = Cell->CustomIndex;
				const int32 FaceIndex = Cell->FaceIndex;

				// Record initial match at depth 0
				PCGExClusters::FCellExpansionData& Data = CellExpansionMap.FindOrAdd(FaceIndex);
				Data.RecordPick(SeedIndex, 0);

				// Expand to adjacent cells
				const int32 Growth = Context->SeedGrowth.GetGrowth(SeedIndex);
				if (Growth > 0)
				{
					ExpandSeedToAdjacentCells(SeedIndex, FaceIndex, Growth);
				}
			}

			// Add expanded cells to ValidCells (cells picked by expansion but not initially)
			TSet<int32> InitialFaceIndices;
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
			{
				if (Cell && Cell->FaceIndex >= 0)
				{
					InitialFaceIndices.Add(Cell->FaceIndex);
				}
			}

			const TSharedPtr<PCGExCells::FSeedOwnershipHandler>& SeedOwnership = Context->SeedOwnership;

			for (const auto& Pair : CellExpansionMap)
			{
				const int32 FaceIndex = Pair.Key;
				const PCGExClusters::FCellExpansionData& ExpData = Pair.Value;

				if (InitialFaceIndices.Contains(FaceIndex))
				{
					// Cell already in ValidCells - just update expansion tracking
					for (TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
					{
						if (Cell && Cell->FaceIndex == FaceIndex)
						{
							Cell->ExpansionPickCount = ExpData.PickCount;
							Cell->ExpansionMinDepth = ExpData.MinDepth;
							break;
						}
					}
				}
				else
				{
					// Find the cell for this face index and add it
					if (TSharedPtr<PCGExClusters::FCell>* CellPtr = FaceIndexToCellMap.Find(FaceIndex))
					{
						if (*CellPtr)
						{
							// Use ownership handler to pick winner among all seeds that expanded to this cell
							TArray<int32> CandidateSeeds = ExpData.SourceIndices.Array();
							const int32 WinnerSeedIndex = SeedOwnership->PickWinner(CandidateSeeds, (*CellPtr)->Data.Centroid);
							(*CellPtr)->CustomIndex = WinnerSeedIndex;
							(*CellPtr)->ExpansionPickCount = ExpData.PickCount;
							(*CellPtr)->ExpansionMinDepth = ExpData.MinDepth;
							ValidCells.Add(*CellPtr);
						}
					}
				}
			}
		}

		// Merge adjacent cells per seed when enabled (only seeds with Growth > 0 can have multiple cells)
		if (Context->SeedGrowth.bMergeAdjacentCells && Context->SeedGrowth.HasPotentialGrowth())
		{
			const TSharedPtr<TArray<FVector2D>> ProjectedPositions = CellsConstraints->Enumerator ? CellsConstraints->Enumerator->GetProjectedPositions() : nullptr;

			TMap<int32, TArray<TSharedPtr<PCGExClusters::FCell>>> CellsBySeed;
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
			{
				if (Cell)
				{
					CellsBySeed.FindOrAdd(Cell->CustomIndex).Add(Cell);
				}
			}

			ValidCells.Reset();

			for (auto& Pair : CellsBySeed)
			{
				const int32 SeedIndex = Pair.Key;
				TArray<TSharedPtr<PCGExClusters::FCell>>& SeedCells = Pair.Value;

				if (SeedCells.Num() <= 1 || Context->SeedGrowth.GetGrowth(SeedIndex) == 0)
				{
					ValidCells.Append(SeedCells);
					continue;
				}

				TArray<TSharedPtr<PCGExClusters::FCell>> Merged = PCGExClusters::MergeAdjacentCells(
					SeedCells, CellsConstraints.ToSharedRef(), Cluster.Get(), ProjectedPositions, SeedIndex);

				ValidCells.Append(Merged.IsEmpty() ? SeedCells : Merged);
			}
		}

		// Classify all valid cells (respecting enable flags)
		for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
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

		// Handle wrapper cell
		if (WrapperCell && (!Settings->Constraints.bOmitWrappingBounds || (Settings->Constraints.bKeepWrapperIfSolePath && ValidCells.IsEmpty())))
		{
			TSet<int32> ConsumedSeeds;
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
			{
				if (Cell)
				{
					ConsumedSeeds.Add(Cell->CustomIndex);
				}
			}

			const int32 NumSeeds = Seeds->Num();
			TConstPCGValueRange<FTransform> SeedTransforms = Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
			const double MaxPlaneDistSq = Settings->SeedPicking.MaxDistance > 0 ? FMath::Square(Settings->SeedPicking.MaxDistance) : -1.0;

			// Arbitration (when it ran) already proves membership in a VALID cell; only failed cells
			// still need testing.
			const bool bArbitrated = !SeedBestFace.IsEmpty();
			const TArray<TSharedPtr<PCGExClusters::FCell>>& ConsumptionCells = bArbitrated ? FailedCellsOnly : AllCellsIncludingFailed;

			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
			{
				if (ConsumedSeeds.Contains(SeedIdx))
				{
					continue;
				}

				if (bArbitrated && SeedBestFace[SeedIdx] != INDEX_NONE)
				{
					ConsumedSeeds.Add(SeedIdx);
					continue;
				}

				const FVector2D& SeedPoint = Seeds->GetProjected(SeedIdx);

				for (const TSharedPtr<PCGExClusters::FCell>& Cell : ConsumptionCells)
				{
					if (Cell && Cell->ContainsPoint(SeedPoint, SeedTransforms[SeedIdx].GetLocation(), MaxPlaneDistSq))
					{
						ConsumedSeeds.Add(SeedIdx);
						break;
					}
				}
			}

			Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Edge);

			const TSharedPtr<PCGExCells::FSeedOwnershipHandler>& SeedOwnership = Context->SeedOwnership;
			TArray<int32> CandidateSeeds;
			CandidateSeeds.Reserve(NumSeeds);

			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
			{
				if (ConsumedSeeds.Contains(SeedIdx))
				{
					continue;
				}

				const FVector& SeedPos = SeedTransforms[SeedIdx].GetLocation();
				double ClosestEdgeDistSq = TNumericLimits<double>::Max();

				Cluster->GetEdgeOctree()->FindNearbyElements(SeedPos, [&](const PCGExOctree::FItem& Item)
				{
					const double DistSq = Cluster->GetPointDistToEdgeSquared(Item.Index, SeedPos);
					if (DistSq < ClosestEdgeDistSq)
					{
						ClosestEdgeDistSq = DistSq;
					}
				});

				if (Settings->SeedPicking.WithinDistanceSquared(ClosestEdgeDistSq))
				{
					CandidateSeeds.Add(SeedIdx);
				}
			}

			// Pick winner using seed ownership handler
			const int32 BestSeedIdx = SeedOwnership->PickWinner(CandidateSeeds, WrapperCell->Data.Centroid);

			if (BestSeedIdx != INDEX_NONE)
			{
				WrapperCell->CustomIndex = BestSeedIdx;

				const ECellTriageResult WrapperResult = ClassifyCell(WrapperCell);

				// Add wrapper to appropriate category if enabled
				switch (WrapperResult)
				{
				case ECellTriageResult::Inside:
					if (Settings->OutputInside())
					{
						CellsInside.Add(WrapperCell);
					}
					break;
				case ECellTriageResult::Touching:
					if (Settings->OutputTouching())
					{
						CellsTouching.Add(WrapperCell);
					}
					break;
				case ECellTriageResult::Outside:
					if (Settings->OutputOutside())
					{
						CellsOutside.Add(WrapperCell);
					}
					break;
				}
			}
		}

		const int32 TotalCells = CellsInside.Num() + CellsTouching.Num() + CellsOutside.Num();
		if (TotalCells == 0)
		{
			bIsProcessorValid = false;
			return;
		}

		// Output CellBounds
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
			OBBPointIO->IOIndex = BatchIndex;
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
				// Combined mode - all go to OutputCellBoundsInside but with different tags
				OutputCellBounds(CellsInside, Context->OutputCellBoundsInside, PCGExCellTriage::TagInside);
				OutputCellBounds(CellsTouching, Context->OutputCellBoundsInside, PCGExCellTriage::TagTouching);
				OutputCellBounds(CellsOutside, Context->OutputCellBoundsInside, PCGExCellTriage::TagOutside);
			}
		}

		// Prepare path outputs
		auto PreparePathOutputs = [&](TArray<TSharedPtr<PCGExClusters::FCell>>& Cells, TArray<TSharedPtr<PCGExData::FPointIO>>& CellsIO, TArray<FString>& CellTags, const TSharedPtr<PCGExData::FPointIOCollection>& OutputCollection, const FString& TriageTag = TEXT(""))
		{
			if (!OutputCollection || Cells.IsEmpty())
			{
				return;
			}

			CellsIO.Reserve(Cells.Num());
			CellTags.Reserve(Cells.Num());
			OutputCollection->IncreaseReserve(Cells.Num() + 1);
			for (int32 i = 0; i < Cells.Num(); i++)
			{
				CellsIO.Add(OutputCollection->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New));
				CellTags.Add(TriageTag);
			}
		};

		if (Settings->Artifacts.bOutputPaths)
		{
			if (Settings->OutputMode == EPCGExCellTriageOutput::Separate)
			{
				PreparePathOutputs(CellsInside, CellsIOInside, CellTagsInside, Context->OutputPathsInside);
				PreparePathOutputs(CellsTouching, CellsIOTouching, CellTagsTouching, Context->OutputPathsTouching);
				PreparePathOutputs(CellsOutside, CellsIOOutside, CellTagsOutside, Context->OutputPathsOutside);
			}
			else
			{
				// Combined mode - all go to OutputPathsInside but with different tags
				PreparePathOutputs(CellsInside, CellsIOInside, CellTagsInside, Context->OutputPathsInside, PCGExCellTriage::TagInside);
				PreparePathOutputs(CellsTouching, CellsIOTouching, CellTagsTouching, Context->OutputPathsInside, PCGExCellTriage::TagTouching);
				PreparePathOutputs(CellsOutside, CellsIOOutside, CellTagsOutside, Context->OutputPathsInside, PCGExCellTriage::TagOutside);
			}

			const int32 PathCount = CellsIOInside.Num() + CellsIOTouching.Num() + CellsIOOutside.Num();
			if (PathCount > 0)
			{
				PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, ProcessCellsTask)

				ProcessCellsTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
				{
					PCGEX_ASYNC_THIS
					const TSharedPtr<PCGExClusters::FCellPathBuilder>& Processor = This->CellProcessor;

					const int32 InsideCount = This->CellsIOInside.Num();
					const int32 TouchingCount = This->CellsIOTouching.Num();

					PCGEX_SCOPE_LOOP(Index)
					{
						if (Index < InsideCount)
						{
							if (const TSharedPtr<PCGExData::FPointIO> IO = This->CellsIOInside[Index])
							{
								Processor->ProcessSeededCell(This->CellsInside[Index], IO, This->CellTagsInside[Index]);
							}
							This->CellsInside[Index] = nullptr;
						}
						else if (Index < InsideCount + TouchingCount)
						{
							const int32 LocalIndex = Index - InsideCount;
							if (const TSharedPtr<PCGExData::FPointIO> IO = This->CellsIOTouching[LocalIndex])
							{
								Processor->ProcessSeededCell(This->CellsTouching[LocalIndex], IO, This->CellTagsTouching[LocalIndex]);
							}
							This->CellsTouching[LocalIndex] = nullptr;
						}
						else
						{
							const int32 LocalIndex = Index - InsideCount - TouchingCount;
							if (const TSharedPtr<PCGExData::FPointIO> IO = This->CellsIOOutside[LocalIndex])
							{
								Processor->ProcessSeededCell(This->CellsOutside[LocalIndex], IO, This->CellTagsOutside[LocalIndex]);
							}
							This->CellsOutside[LocalIndex] = nullptr;
						}
					}
				};

				ProcessCellsTask->StartSubLoops(PathCount, 64);
			}
		}
	}

	void FProcessor::ExpandSeedToAdjacentCells(int32 SeedIndex, int32 InitialFaceIndex, int32 MaxGrowth)
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

			// Record this cell selection
			PCGExClusters::FCellExpansionData& Data = CellExpansionMap.FindOrAdd(FaceIndex);
			Data.RecordPick(SeedIndex, Depth);

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
		TProcessor<FPCGExFindContoursBoundedContext, UPCGExFindContoursBoundedSettings>::Cleanup();
		if (CellsConstraints)
		{
			CellsConstraints->Cleanup();
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
