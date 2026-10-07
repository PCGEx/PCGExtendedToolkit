// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindCells.h"

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
#include "Helpers/PCGExSeededCellResolver.h"
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

	PCGEX_FWD(SeedMerge)
	Context->SeedMerge.Init(Context, Context->SeedsDataFacade);

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

		// Build or get the shared enumerator from constraints (enables reuse)
		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		// Enumerate all cells, also get failed cells for consumption tracking
		// Wrapper detected by winding (CW face) and stored in constraints
		TArray<TSharedPtr<PCGExClusters::FCell>> AllCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCells;
		Enumerator->EnumerateAllFaces(AllCells, CellsConstraints.ToSharedRef(), &FailedCells, true);
		WrapperCell = CellsConstraints->WrapperCell;

		PCGExCells::FSeededCellResolver::FConfig ResolverConfig;
		ResolverConfig.Context = Context;
		ResolverConfig.Cluster = Cluster;
		ResolverConfig.SeedsDataFacade = Context->SeedsDataFacade;
		ResolverConfig.Projection = &ProjectionDetails;
		ResolverConfig.SeedPicking = &Settings->SeedPicking;
		ResolverConfig.SeedOwnership = Context->SeedOwnership.Get();
		ResolverConfig.SeedGrowth = &Context->SeedGrowth;
		ResolverConfig.SeedMerge = &Context->SeedMerge;
		ResolverConfig.bSoloClusterWorkload = bSoloClusterWorkload;

		Resolver = MakeShared<PCGExCells::FSeededCellResolver>(ResolverConfig);
		Resolver->SetCells(MoveTemp(AllCells), MoveTemp(FailedCells), CellsConstraints.ToSharedRef(), Enumerator.ToSharedRef());

		// Process cells in parallel to find which seeds they contain.
		// Without a cell there is no range to loop over: only the wrapper is left to claim.
		if (Resolver->NumCells() > 0)
		{
			StartParallelLoopForRange(Resolver->NumCells(), 64);
		}
		else
		{
			OnRangeProcessingComplete();
		}

		return true;
	}

	void FProcessor::PrepareLoopScopesForRanges(const TArray<PCGExMT::FScope>& Loops)
	{
		Resolver->PrepareScopes(Loops);
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		Resolver->ProcessRange(Scope);
	}

	void FProcessor::OnRangeProcessingComplete()
	{
		Resolver->Finalize(ValidCells);

		// Check if any exterior seeds can claim the wrapper
		// Include wrapper if: not omitting wrapping bounds, OR (omitting but keep-if-sole is on AND no other valid cells)
		if (WrapperCell && (!Settings->Constraints.bOmitWrappingBounds || (Settings->Constraints.bKeepWrapperIfSolePath && ValidCells.IsEmpty())))
		{
			const int32 WrapperSeedIdx = Resolver->PickWrapperSeed(ValidCells, WrapperCell->Data.Centroid);
			if (WrapperSeedIdx != INDEX_NONE)
			{
				WrapperCell->CustomIndex = WrapperSeedIdx;
				ValidCells.Add(WrapperCell);
			}
		}

		Resolver.Reset();

		const int32 NumCells = ValidCells.Num();
		if (NumCells == 0)
		{
			bIsProcessorValid = false;
			return;
		}

		CellProcessor->MarkSeedsGood(ValidCells);

		if (Settings->Artifacts.bOutputCellBounds)
		{
			// Batch, then vtx dataset: BatchIndex restarts per vtx input.
			if (!PCGExClusters::ProcessCellsAsOBBPoints(
				Cluster, ValidCells, Context->OutputCellBounds, VtxDataFacade->Source,
				PCGExData::FIOSortKey{BatchIndex, VtxDataFacade->Source->IOIndex}, Context->Artifacts, TaskManager))
			{
				return;
			}
		}

		if (Settings->Artifacts.bOutputPaths)
		{
			CellsIOIndices.SetNum(NumCells);
			if (!Context->OutputPaths->EmplaceBatch<UPCGPointArrayData>(CellsIOIndices, VtxDataFacade->Source, PCGExData::EIOInit::New))
			{
				return;
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
						Processor->ProcessSeededCell(ValidCells_Ref[Index], IO, TEXT(""), Index);
					}
					ValidCells_Ref[Index] = nullptr;
				}
			};

			ProcessCellsTask->StartSubLoops(CellsIOIndices.Num(), 64);
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
