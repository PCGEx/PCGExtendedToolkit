// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingFindCellsBounded.h"

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
#include "Helpers/PCGExSeededCellResolver.h"
#include "Math/Geo/PCGExGeo.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"

#define LOCTEXT_NAMESPACE "PCGExFindContoursBounded"
#define PCGEX_NAMESPACE FindContoursBounded

TArray<FPCGPinProperties> UPCGExFindContoursBoundedSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seeds associated with the main input points", Required)
	PCGEX_PIN_SPATIAL(PCGExCellTriage::Labels::SourceBounds, "Spatial data whose bounds will be used to triage cells", Required)
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

	PCGExCellTriage::DeclareOutputPins(PinProperties, OutputMode, TriageFlags, Artifacts.bOutputPaths, Artifacts.bOutputCellBounds);

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

	PCGEX_FWD(SeedMerge)
	Context->SeedMerge.Init(Context, Context->SeedsDataFacade);

	Context->SeedOwnership = MakeShared<PCGExCells::FSeedOwnershipHandler>();
	Context->SeedOwnership->Method = Settings->SeedOwnership;
	Context->SeedOwnership->SortDirection = Settings->SortDirection;
	if (!Context->SeedOwnership->Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}

	if (!Context->Triage.Init(
		Context, PCGExCellTriage::Labels::SourceBounds, Settings->OutputMode, Settings->TriageFlags,
		Settings->Artifacts.bOutputPaths, Settings->Artifacts.bOutputCellBounds))
	{
		return false;
	}

	PCGEX_FWD(SeedAttributesToPathTags)
	if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}
	Context->SeedForwardHandler = Settings->SeedForwarding.GetHandler(Context->SeedsDataFacade);

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

	(void)Context->Triage.StageOutputs(Context->OutputData.InactiveOutputPinBitmask);

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

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindContoursBounded::Process);

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

		TSharedPtr<PCGExClusters::FPlanarFaceEnumerator> Enumerator = CellsConstraints->GetOrBuildEnumerator(Cluster.ToSharedRef(), ProjectionDetails);

		TArray<TSharedPtr<PCGExClusters::FCell>> AllCells;
		TArray<TSharedPtr<PCGExClusters::FCell>> FailedCells;
		const bool bNeedOutside = Settings->OutputOutside();
		Enumerator->EnumerateFacesWithinBounds(
			AllCells,
			CellsConstraints.ToSharedRef(),
			Context->Triage.BoundsFilter,
			bNeedOutside, // Only include outside faces if user wants them
			&FailedCells,
			true);
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
		TArray<TSharedPtr<PCGExClusters::FCell>> ClaimedCells;
		Resolver->Finalize(ClaimedCells);

		// Classify the claimed cells; a category that is not wanted drops its cells.
		Buckets.Add(ClaimedCells, Context->Triage);

		if (WrapperCell && (!Settings->Constraints.bOmitWrappingBounds || (Settings->Constraints.bKeepWrapperIfSolePath && ClaimedCells.IsEmpty())))
		{
			const int32 WrapperSeedIdx = Resolver->PickWrapperSeed(ClaimedCells, WrapperCell->Data.Centroid);
			if (WrapperSeedIdx != INDEX_NONE)
			{
				WrapperCell->CustomIndex = WrapperSeedIdx;
				Buckets.Add(WrapperCell, Context->Triage);
			}
		}

		Resolver.Reset();

		if (Buckets.NumCells() == 0)
		{
			bIsProcessorValid = false;
			return;
		}

		for (const TArray<TSharedPtr<PCGExClusters::FCell>>& Cells : Buckets.Cells)
		{
			CellProcessor->MarkSeedsGood(Cells);
		}

		if (Settings->Artifacts.bOutputCellBounds)
		{
			// Batch, then vtx dataset: BatchIndex restarts per vtx input.
			if (!Buckets.EmitBounds(
				Cluster, VtxDataFacade->Source, PCGExData::FIOSortKey{BatchIndex, VtxDataFacade->Source->IOIndex},
				Context->Triage, Context->Artifacts, TaskManager))
			{
				return;
			}
		}

		if (Settings->Artifacts.bOutputPaths)
		{
			if (!Buckets.PreparePaths(VtxDataFacade->Source, Context->Triage))
			{
				return;
			}

			const int32 NumPaths = Buckets.NumPaths();
			if (NumPaths > 0)
			{
				PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, ProcessCellsTask)

				ProcessCellsTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
				{
					PCGEX_ASYNC_THIS
					PCGExCellTriage::FBuckets& Buckets_Ref = This->Buckets;
					const PCGExCellTriage::FOutputs& Triage_Ref = This->Context->Triage;
					const TSharedPtr<PCGExClusters::FCellPathBuilder>& Processor = This->CellProcessor;

					PCGEX_SCOPE_LOOP(Index)
					{
						EPCGExCellTriageResult Category = EPCGExCellTriageResult::Inside;
						int32 CellIndex = INDEX_NONE;
						Buckets_Ref.Resolve(Index, Category, CellIndex);

						const int32 CategoryIndex = PCGExCellTriage::GetCategoryIndex(Category);
						TSharedPtr<PCGExClusters::FCell>& Cell = Buckets_Ref.Cells[CategoryIndex][CellIndex];

						if (const TSharedPtr<PCGExData::FPointIO>& IO = Buckets_Ref.PathIOs[CategoryIndex][CellIndex])
						{
							Processor->ProcessSeededCell(Cell, IO, Triage_Ref.GetTag(Category), Index);
						}

						Cell = nullptr;
					}
				};

				ProcessCellsTask->StartSubLoops(NumPaths, 64);
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
