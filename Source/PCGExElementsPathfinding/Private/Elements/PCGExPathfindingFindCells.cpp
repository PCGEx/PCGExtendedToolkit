// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindCells.h"

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
#include "Data/Utils/PCGExDataForward.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExFindContours"
#define PCGEX_NAMESPACE FindContours

TArray<FPCGPinProperties> UPCGExFindContoursSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seeds associated with the main input points", Required)
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, SeedOwnership == EPCGExCellSeedOwnership::BestCandidate ? EPCGPinStatus::Required : EPCGPinStatus::Advanced);
	return PinProperties;
}

bool UPCGExFindContoursSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExSorting::Labels::SourceSortingRules)
	{
		return SeedOwnership == EPCGExCellSeedOwnership::BestCandidate;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExFindContoursSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (Artifacts.bOutputPaths)
	{
		PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cell contours as closed paths", Required)
	}
	else
	{
		PCGEX_PIN_POINTS(PCGExCells::OutputLabels::Paths, "Cell contours as closed paths", Advanced)
	}

	if (Artifacts.bOutputCellBounds)
	{
		PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cell OBB bounds as points", Required)
	}
	else
	{
		PCGEX_PIN_POINTS(PCGExCells::OutputLabels::CellBounds, "Cell OBB bounds as points", Advanced)
	}

	if (bOutputFilteredSeeds)
	{
		PCGEX_PIN_POINT(PCGExFindContours::OutputGoodSeedsLabel, "GoodSeeds", Required)
		PCGEX_PIN_POINT(PCGExFindContours::OutputBadSeedsLabel, "BadSeeds", Required)
	}
	return PinProperties;
}

PCGExData::EIOInit UPCGExFindContoursSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExFindContoursSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(FindContours)
PCGEX_ELEMENT_BATCH_EDGE_IMPL(FindContours)

bool FPCGExFindContoursElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FindContours)

	PCGEX_FWD(Artifacts)
	PCGEX_FWD(SeedGrowth)

	// Initialize Artifacts (output settings + OBB settings)
	if (!Context->Artifacts.Init(Context))
	{
		return false;
	}

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	// Initialize seed growth (will read per-point growth attribute if needed)
	Context->SeedGrowth.Init(Context, Context->SeedsDataFacade);

	// Initialize seed ownership handler
	Context->SeedOwnership = MakeShared<PCGExCells::FSeedOwnershipHandler>();
	Context->SeedOwnership->Method = Settings->SeedOwnership;
	Context->SeedOwnership->SortDirection = Settings->SortDirection;
	if (!Context->SeedOwnership->Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}

	PCGEX_FWD(SeedAttributesToPathTags)
	if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}
	Context->SeedForwardHandler = Settings->SeedForwarding.GetHandler(Context->SeedsDataFacade);

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = PCGExCells::OutputLabels::Paths;

	Context->OutputCellBounds = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputCellBounds->OutputPin = PCGExCells::OutputLabels::CellBounds;

	if (Settings->bOutputFilteredSeeds)
	{
		const int32 NumSeeds = Context->SeedsDataFacade->GetNum();

		Context->SeedQuality.Init(false, NumSeeds);

		Context->GoodSeeds = NewPointIO(Context->SeedsDataFacade->Source, PCGExFindContours::OutputGoodSeedsLabel);
		Context->GoodSeeds->InitializeOutput(PCGExData::EIOInit::Duplicate);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Context->GoodSeeds->GetOut(), NumSeeds);

		Context->BadSeeds = NewPointIO(Context->SeedsDataFacade->Source, PCGExFindContours::OutputBadSeedsLabel);
		Context->BadSeeds->InitializeOutput(PCGExData::EIOInit::Duplicate);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Context->BadSeeds->GetOut(), NumSeeds);
	}

	return true;
}

bool FPCGExFindContoursElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindContoursElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindContours)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				//NewBatch->bRequiresWriteStep = Settings->Artifacts.WriteAny();
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

	if (Settings->bOutputFilteredSeeds)
	{
		(void)Context->GoodSeeds->Gather(Context->SeedQuality);
		(void)Context->BadSeeds->Gather(Context->SeedQuality, true);

		(void)Context->GoodSeeds->StageOutput(Context);
		(void)Context->BadSeeds->StageOutput(Context);
	}

	return Context->TryComplete();
}


namespace PCGExFindContours
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindContours::Process);

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

		// Set up cell constraints
		CellsConstraints = MakeShared<PCGExClusters::FCellConstraints>(Settings->Constraints);
		CellsConstraints->Reserve(Cluster->Edges->Num());
		// Face planes cost a fit per cell; only pay when the distance gate reads them (LocalTangent
		// always builds its own -- the polygon lives in that frame).
		CellsConstraints->bComputeFacePlanes = Settings->SeedPicking.MaxDistance > 0;

		// Build or get the shared enumerator from constraints (enables reuse)
		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		// Enumerate all cells, also get failed cells for consumption tracking
		// Wrapper detected by winding (CW face) and stored in constraints
		TArray<TSharedPtr<PCGExClusters::FCell>> AllCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCells;
		Enumerator->EnumerateAllFaces(AllCells, CellsConstraints.ToSharedRef(), &FailedCells, true);
		WrapperCell = CellsConstraints->WrapperCell;

		// Build adjacency map if growth is enabled
		if (Context->SeedGrowth.HasPotentialGrowth())
		{
			// Get wrapper face index to exclude from adjacency
			int32 WrapperFaceIndex = Enumerator->GetWrapperFaceIndex();
			CellAdjacencyMap = Enumerator->GetOrBuildAdjacencyMap(WrapperFaceIndex);

			// Build FaceIndex -> Cell map for all valid cells
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : AllCells)
			{
				if (Cell && Cell->FaceIndex >= 0)
				{
					FaceIndexToCellMap.Add(Cell->FaceIndex, Cell);
				}
			}
		}

		// Create projected seed set (lazy projection with AABB)
		Seeds = MakeShared<PCGExClusters::FProjectedPointSet>(Context, Context->SeedsDataFacade.ToSharedRef(), ProjectionDetails);
		Seeds->EnsureProjected(); // Project once upfront before any loops

		SeedInBounds.Init(true, NumSeeds);
		if (Settings->SeedPicking.bWithinClusterBounds)
		{
			TConstPCGValueRange<FTransform> SeedTransforms = Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
			for (int32 i = 0; i < NumSeeds; i++)
			{
				SeedInBounds[i] = Settings->SeedPicking.WithinBounds(Cluster->Bounds, SeedTransforms[i].GetLocation());
			}
		}

		// Combine valid and failed internal cells for consumption tracking
		// (seeds inside ANY internal cell polygon are "consumed" - can't claim wrapper)
		AllCellsIncludingFailed = AllCells;
		AllCellsIncludingFailed.Append(FailedCells);

		if (AllCells.IsEmpty() && WrapperCell)
		{
			// No valid internal cells - check if any seed can claim wrapper
			HandleWrapperOnlyCase(NumSeeds);
			return true;
		}

		// Store valid cells for parallel processing
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
					if (!SeedInBounds[SeedIdx])
					{
						return;
					}

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

		// Process cells in parallel to find which seeds they contain
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
					if (!SeedInBounds[SeedIdx])
					{
						continue;
					}

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
		// No valid internal cells exist - check if exterior seeds can claim wrapper
		if (!WrapperCell)
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
			if (!SeedInBounds[SeedIdx])
			{
				continue;
			}

			// Check if seed is inside any internal cell (consumed)
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

			// Seed is exterior - find closest edge distance
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

			TArray<TSharedPtr<PCGExClusters::FCell>> WrapperArray;
			WrapperArray.Add(WrapperCell);

			// Output to CellBounds if enabled
			if (Settings->Artifacts.bOutputCellBounds)
			{
				TSharedPtr<PCGExData::FPointIO> OBBPointIO = Context->OutputCellBounds->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
				if (!OBBPointIO)
				{
					return;
				}

				OBBPointIO->IOIndex = BatchIndex;
				PCGExClusters::Helpers::CleanupClusterData(OBBPointIO);

				PCGEX_MAKE_SHARED(OBBFacade, PCGExData::FFacade, OBBPointIO.ToSharedRef())
				PCGExClusters::ProcessCellsAsOBBPoints(Cluster, WrapperArray, OBBFacade,
				                                       Context->Artifacts, TaskManager);
			}

			// Output to Paths if enabled
			if (Settings->Artifacts.bOutputPaths)
			{
				CellProcessor->ProcessSeededCell(WrapperCell, Context->OutputPaths->Emplace_GetRef<UPCGPointArrayData>(VtxDataFacade->Source, PCGExData::EIOInit::New));
			}
		}
	}

	void FProcessor::OnRangeProcessingComplete()
	{
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

		int32 NumCells = ValidCells.Num();

		// Check if any exterior seeds can claim the wrapper
		// Include wrapper if: not omitting wrapping bounds, OR (omitting but keep-if-sole is on AND no other valid cells)
		if (WrapperCell && (!Settings->Constraints.bOmitWrappingBounds || (Settings->Constraints.bKeepWrapperIfSolePath && NumCells == 0)))
		{
			// Collect consumed seed indices (seeds that matched a valid internal cell)
			TSet<int32> ConsumedSeeds;
			for (const TSharedPtr<PCGExClusters::FCell>& Cell : ValidCells)
			{
				if (Cell)
				{
					ConsumedSeeds.Add(Cell->CustomIndex);
				}
			}

			// Also mark seeds inside failed cells as consumed. Arbitration (when it ran) already proves
			// membership in a VALID cell, so only the failed cells still need testing.
			const int32 NumSeeds = Seeds->Num();
			TConstPCGValueRange<FTransform> SeedTransforms = Context->SeedsDataFacade->GetIn()->GetConstTransformValueRange();
			const double MaxPlaneDistSq = Settings->SeedPicking.MaxDistance > 0 ? FMath::Square(Settings->SeedPicking.MaxDistance) : -1.0;

			const bool bArbitrated = !SeedBestFace.IsEmpty();
			const TArray<TSharedPtr<PCGExClusters::FCell>>& ConsumptionCells = bArbitrated ? FailedCellsOnly : AllCellsIncludingFailed;

			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
			{
				if (!SeedInBounds[SeedIdx] || ConsumedSeeds.Contains(SeedIdx))
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

			// Find best exterior seed within picking distance
			Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Edge);

			const TSharedPtr<PCGExCells::FSeedOwnershipHandler>& SeedOwnership = Context->SeedOwnership;
			TArray<int32> CandidateSeeds;
			CandidateSeeds.Reserve(NumSeeds);

			for (int32 SeedIdx = 0; SeedIdx < NumSeeds; ++SeedIdx)
			{
				if (!SeedInBounds[SeedIdx] || ConsumedSeeds.Contains(SeedIdx))
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
				ValidCells.Add(WrapperCell);
				NumCells++;
			}
		}

		if (NumCells == 0)
		{
			bIsProcessorValid = false;
			return;
		}

		// Output to CellBounds if enabled
		if (Settings->Artifacts.bOutputCellBounds)
		{
			TSharedPtr<PCGExData::FPointIO> OBBPointIO = Context->OutputCellBounds->Emplace_GetRef(VtxDataFacade->Source, PCGExData::EIOInit::New);
			if (!OBBPointIO)
			{
				return;
			}

			OBBPointIO->IOIndex = BatchIndex;
			PCGExClusters::Helpers::CleanupClusterData(OBBPointIO);

			PCGEX_MAKE_SHARED(OBBFacade, PCGExData::FFacade, OBBPointIO.ToSharedRef())
			PCGExClusters::ProcessCellsAsOBBPoints(Cluster, ValidCells, OBBFacade,
			                                       Context->Artifacts, TaskManager);
		}

		// Output to Paths if enabled
		if (Settings->Artifacts.bOutputPaths)
		{
			CellsIOIndices.SetNum(NumCells);
			if (!Context->OutputPaths->EmplaceBatch<UPCGPointArrayData>(CellsIOIndices, VtxDataFacade->Source, PCGExData::EIOInit::New))
			{
				return;
			}
			
			for (const TSharedPtr<PCGExData::FPointIO>& IO : CellsIOIndices)
			{
				PCGExClusters::Helpers::CleanupClusterData(IO);
			}

			PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, ProcessCellsTask)

			ProcessCellsTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				TArray<TSharedPtr<PCGExClusters::FCell>>& ValidCells_Ref = This->ValidCells;
				const TArray<TSharedPtr<PCGExData::FPointIO>>& CellsIOIndices_Ref = This->CellsIOIndices;
				const TSharedPtr<PCGExClusters::FCellPathBuilder>& Processor = This->CellProcessor;

				PCGEX_SCOPE_LOOP(Index)
				{
					if (const TSharedPtr<PCGExData::FPointIO> IO = CellsIOIndices_Ref[Index])
					{
						Processor->ProcessSeededCell(ValidCells_Ref[Index], IO);
					}
					ValidCells_Ref[Index] = nullptr;
				}
			};

			ProcessCellsTask->StartSubLoops(CellsIOIndices.Num(), 64);
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
		TProcessor<FPCGExFindContoursContext, UPCGExFindContoursSettings>::Cleanup();
		if (CellsConstraints)
		{
			CellsConstraints->Cleanup();
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
