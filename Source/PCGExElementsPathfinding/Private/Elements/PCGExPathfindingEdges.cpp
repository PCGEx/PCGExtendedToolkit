// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingEdges.h"


#include "PCGExHeuristicsCommon.h"
#include "PCGExHeuristicsHandler.h"
#include "PCGParamData.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Core/PCGExPathQuery.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Data/Utils/PCGExDataForward.h"
#include "GoalPickers/PCGExGoalPickerRandom.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Paths/PCGExPath.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"
#include "Search/PCGExSearchDijkstra.h"
#include "Search/PCGExSearchOperation.h"

#define LOCTEXT_NAMESPACE "PCGExPathfindingEdgesElement"
#define PCGEX_NAMESPACE PathfindingEdges

#if WITH_EDITOR
void UPCGExPathfindingEdgesSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!GoalPicker)
		{
			GoalPicker = NewObject<UPCGExGoalPicker>(this, TEXT("GoalPicker"));
		}
		if (!SearchAlgorithm)
		{
			SearchAlgorithm = NewObject<UPCGExSearchDijkstra>(this, TEXT("SearchAlgorithm"));
		}
	}
	Super::PostInitProperties();
}

void UPCGExPathfindingEdgesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (GoalPicker)
	{
		GoalPicker->UpdateUserFacingInfos();
	}
	if (SearchAlgorithm)
	{
		SearchAlgorithm->UpdateUserFacingInfos();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
void FPCGExPathfindingEdgesContext::BuildPath(const TSharedPtr<PCGExPathfinding::FPathQuery>& Query, const TSharedPtr<PCGExData::FPointIO>& PathIO)
{
	if (!PathIO)
	{
		return;
	}

	PCGEX_SETTINGS_LOCAL(PathfindingEdges)

	TArray<int32> PathIndices;
	int32 ExtraIndices = 0;
	PathIndices.Reserve(Query->PathNodes.Num() + 2);

	if (Settings->PathComposition == EPCGExPathComposition::Vtx)
	{
		Query->AppendNodePoints(PathIndices);
		if (PathIndices.Num() < 2)
		{
			return;
		}
	}
	else if (Settings->PathComposition == EPCGExPathComposition::Edges)
	{
		Query->AppendEdgePoints(PathIndices);
		if (PathIndices.Num() < 1)
		{
			return;
		}
	}
	else if (Settings->PathComposition == EPCGExPathComposition::VtxAndEdges)
	{
		// TODO : Implement
	}

	if (Settings->bAddSeedToPath)
	{
		ExtraIndices++;
	}
	if (Settings->bAddGoalToPath)
	{
		ExtraIndices++;
	}

	if (!Settings->PathOutputDetails.Validate(PathIndices.Num() + ExtraIndices))
	{
		return;
	}

	PathIO->Enable();

	EPCGPointNativeProperties AllocateProperties = PathIO->GetIn()->GetAllocatedProperties();
	EnumAddFlags(AllocateProperties, SeedsDataFacade->GetAllocations());
	EnumAddFlags(AllocateProperties, GoalsDataFacade->GetAllocations());

	PathIO->IOIndex = Query->QueryIndex;
	UPCGBasePointData* PathPoints = PathIO->GetOut();
	PCGExPointArrayDataHelpers::SetNumPointsAllocated(PathPoints, PathIndices.Num() + ExtraIndices, AllocateProperties);

	PathIO->InheritPoints(PathIndices, Settings->bAddSeedToPath ? 1 : 0);

	if (Settings->bAddSeedToPath)
	{
		Query->Seed.Point.Data->CopyPropertiesTo(PathPoints, Query->Seed.Point.Index, 0, 1, AllocateProperties & ~EPCGPointNativeProperties::MetadataEntry);
	}

	if (Settings->bAddGoalToPath)
	{
		Query->Goal.Point.Data->CopyPropertiesTo(PathPoints, Query->Goal.Point.Index, PathPoints->GetNumPoints() - 1, 1, AllocateProperties & ~EPCGPointNativeProperties::MetadataEntry);
	}

	PCGExClusters::Helpers::CleanupClusterData(PathIO);

	PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, PathIO.ToSharedRef())

	SeedAttributesToPathTags.Tag(Query->Seed, PathIO);
	GoalAttributesToPathTags.Tag(Query->Goal, PathIO);

	SeedForwardHandler->Forward(Query->Seed.Point.Index, PathDataFacade);
	GoalForwardHandler->Forward(Query->Goal.Point.Index, PathDataFacade);

	PCGExPaths::Helpers::SetClosedLoop(PathIO, false);

	PathDataFacade->WriteFastest(GetTaskManager());
}

PCGEX_INITIALIZE_ELEMENT(PathfindingEdges)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(PathfindingEdges)

bool UPCGExPathfindingEdgesSettings::SupportsDataStealing() const
{
	return OutputMode == EPCGExPathfindingOutputMode::Visited;
}

TArray<FPCGPinProperties> UPCGExPathfindingEdgesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExCommon::Labels::SourceSeedsLabel, "Seeds points for pathfinding.", Required)
	PCGEX_PIN_POINT(PCGExClusters::Labels::SourceGoalsLabel, "Goals points for pathfinding.", Required)
	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExPathfinding::Labels::SourceOverridesGoalPicker)
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExPathfinding::Labels::SourceOverridesSearch)
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPathfindingEdgesSettings::OutputPinProperties() const
{
	if (OutputMode == EPCGExPathfindingOutputMode::Visited)
	{
		// Forward the cluster (Vtx + Edges) carrying the visited counts.
		return Super::OutputPinProperties();
	}

	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Paths output.", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExPathfindingEdgesSettings::GetMainOutputInitMode() const
{
	if (OutputMode == EPCGExPathfindingOutputMode::Visited)
	{
		// Duplicate when we write counts onto the vtx, otherwise forward untouched.
		return (Statistics.bWritePointUseCount && !WantsDataStealing()) ? PCGExData::EIOInit::Duplicate : PCGExData::EIOInit::Forward;
	}
	return PCGExData::EIOInit::NoInit;
}

PCGExData::EIOInit UPCGExPathfindingEdgesSettings::GetEdgeOutputInitMode() const
{
	if (OutputMode == EPCGExPathfindingOutputMode::Visited)
	{
		// Duplicate when we write counts onto the edges, otherwise forward untouched.
		return (Statistics.bWriteEdgeUseCount && !WantsDataStealing()) ? PCGExData::EIOInit::Duplicate : PCGExData::EIOInit::Forward;
	}
	return PCGExData::EIOInit::NoInit;
}

bool FPCGExPathfindingEdgesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingEdges)

	if (Settings->OutputMode == EPCGExPathfindingOutputMode::Visited)
	{
		PCGEX_VALIDATE_NAME_CONDITIONAL(Settings->Statistics.bWritePointUseCount, Settings->Statistics.PointUseCountAttributeName)
		PCGEX_VALIDATE_NAME_CONDITIONAL(Settings->Statistics.bWriteEdgeUseCount, Settings->Statistics.EdgeUseCountAttributeName)
	}

	PCGEX_BIND_INSTANCED_FACTORY(GoalPicker, UPCGExGoalPicker, PCGExPathfinding::Labels::SourceOverridesGoalPicker)
	PCGEX_BIND_INSTANCED_FACTORY(SearchAlgorithm, UPCGExSearchInstancedFactory, PCGExPathfinding::Labels::SourceOverridesSearch)

	Context->SeedsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExCommon::Labels::SourceSeedsLabel, false, true);
	if (!Context->SeedsDataFacade)
	{
		return false;
	}

	Context->GoalsDataFacade = PCGExData::TryGetSingleFacade(Context, PCGExClusters::Labels::SourceGoalsLabel, false, true);
	if (!Context->GoalsDataFacade)
	{
		return false;
	}

	PCGEX_FWD(SeedAttributesToPathTags)
	PCGEX_FWD(GoalAttributesToPathTags)

	if (!Context->SeedAttributesToPathTags.Init(Context, Context->SeedsDataFacade))
	{
		return false;
	}
	if (!Context->GoalAttributesToPathTags.Init(Context, Context->GoalsDataFacade))
	{
		return false;
	}


	auto ValidIdentity = [](const PCGExData::FAttributeIdentity& Identity)
	{
		return Identity.GetIdentifier() != PCGExPaths::Labels::ClosedLoopIdentifier;
	};

	Context->SeedForwardHandler = Settings->SeedForwarding.GetHandler(Context->SeedsDataFacade);
	Context->SeedForwardHandler->ValidateIdentities(ValidIdentity);

	Context->GoalForwardHandler = Settings->GoalForwarding.GetHandler(Context->GoalsDataFacade);
	Context->GoalForwardHandler->ValidateIdentities(ValidIdentity);

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = PCGExPaths::Labels::OutputPathsLabel;

	// Prepare path seed/goal pairs

	if (!Context->GoalPicker->PrepareForData(Context, Context->SeedsDataFacade, Context->GoalsDataFacade))
	{
		return false;
	}

	PCGExPathfinding::ProcessGoals(Context->SeedsDataFacade, Context->GoalPicker, [&](const int32 SeedIndex, const int32 GoalIndex)
	{
		Context->SeedGoalPairs.Add(PCGEx::H64(SeedIndex, GoalIndex));
	});

	if (Context->SeedGoalPairs.IsEmpty())
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("Could not generate any seed/goal pairs."));
		return false;
	}

	return true;
}

bool FPCGExPathfindingEdgesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathfindingEdgesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingEdges)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		const bool bVisited = Settings->OutputMode == EPCGExPathfindingOutputMode::Visited;
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
				NewBatch->SetWantsHeuristics(true, Settings->HeuristicScoreMode);
				if (bVisited)
				{
					NewBatch->bRequiresWriteStep = true;
					NewBatch->bWriteVtxDataFacade = Settings->Statistics.bWritePointUseCount;
				}
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->OutputMode == EPCGExPathfindingOutputMode::Visited)
	{
		Context->OutputPointsAndEdges();
	}
	else
	{
		Context->OutputPaths->StageOutputs();
	}

	return Context->TryComplete();
}


namespace PCGExPathfindingEdges
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathfindingEdges::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const bool bVisited = Settings->OutputMode == EPCGExPathfindingOutputMode::Visited;

		if (Settings->bUseOctreeSearch)
		{
			if (Settings->SeedPicking.PickingMethod == EPCGExClusterClosestSearchMode::Vtx || Settings->GoalPicking.PickingMethod == EPCGExClusterClosestSearchMode::Vtx)
			{
				Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Vtx);
			}

			if (Settings->SeedPicking.PickingMethod == EPCGExClusterClosestSearchMode::Edge || Settings->GoalPicking.PickingMethod == EPCGExClusterClosestSearchMode::Edge)
			{
				Cluster->RebuildOctree(EPCGExClusterClosestSearchMode::Edge);
			}
		}

		SearchOperation = Context->SearchAlgorithm->CreateOperation(); // Create a local copy
		SearchOperation->PrepareForCluster(Cluster.Get());

		bForceSingleThreadedProcessRange = HeuristicsHandler->HasGlobalFeedback() || !Settings->bGreedyQueries;
		if (bForceSingleThreadedProcessRange)
		{
			SearchAllocations = SearchOperation->NewAllocations();
		}

		const int32 NumQueries = Context->SeedGoalPairs.Num();

		// A single early-exit query may explore only a fraction of the cluster; anything more
		// re-evaluates edges enough times for the one-time bake sweep to pay for itself.
		if (NumQueries > 1 || !SearchOperation->bEarlyExit)
		{
			HeuristicsHandler->BakeStaticEdgeScores();
		}

		PCGExArrayHelpers::InitArray(Queries, NumQueries);

		if (bVisited)
		{
			// Per-edge visited buffer lives on this processor's (per-cluster) edge facade.
			// The vtx buffer is batch-shared and wired in via FBatch::PrepareSingle.
			if (Settings->Statistics.bWriteEdgeUseCount)
			{
				VisitedEdgeWriter = EdgeDataFacade->GetWritable<int32>(Settings->Statistics.EdgeUseCountAttributeName, 0, true, Settings->Statistics.bResetValues ? PCGExData::EBufferInit::New : PCGExData::EBufferInit::Inherit);
				if (VisitedEdgeWriter) { VisitedEdgeData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(VisitedEdgeWriter)->GetOutValues()->GetData(); }
			}
		}
		else
		{
			TSharedPtr<PCGExData::FPointIO> ReferenceIO = nullptr;

			if (Settings->PathComposition == EPCGExPathComposition::Vtx)
			{
				ReferenceIO = VtxDataFacade->Source;
			}
			else if (Settings->PathComposition == EPCGExPathComposition::Edges)
			{
				ReferenceIO = EdgeDataFacade->Source;
			}
			else if (Settings->PathComposition == EPCGExPathComposition::VtxAndEdges)
			{
				// TODO : Implement
			}

			QueriesIO.Init(nullptr, NumQueries);

			if (!Context->OutputPaths->EmplaceBatch<UPCGPointArrayData>(QueriesIO, ReferenceIO, PCGExData::EIOInit::New))
			{
				return false;
			}
			
			for (const TSharedPtr<PCGExData::FPointIO>& IO : QueriesIO)
			{
				PCGExClusters::Helpers::CleanupClusterData(IO);
			}
		}

		for (int i = 0; i < NumQueries; i++)
		{
			Queries[i] = MakeShared<PCGExPathfinding::FPathQuery>(Cluster.ToSharedRef(), Context->SeedsDataFacade->Source->GetInPoint(PCGEx::H64A(Context->SeedGoalPairs[i])), Context->GoalsDataFacade->Source->GetInPoint(PCGEx::H64B(Context->SeedGoalPairs[i])), i);
			if (!bVisited)
			{
				QueriesIO[i]->Disable();
			}
		}

		StartParallelLoopForRange(Queries.Num(), bForceSingleThreadedProcessRange ? 12 : 1);
		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		const bool bVisited = Settings->OutputMode == EPCGExPathfindingOutputMode::Visited;

		// Single-threaded mode shares one allocation set across all scopes; otherwise lease
		// pooled allocations for this scope instead of allocating fresh ones per query.
		TSharedPtr<PCGExPathfinding::FSearchAllocations> ScopedAllocations = SearchAllocations;
		const bool bPooled = !ScopedAllocations;
		if (bPooled)
		{
			ScopedAllocations = SearchOperation->AcquireAllocations();
		}

		ON_SCOPE_EXIT
		{
			if (bPooled)
			{
				SearchOperation->ReleaseAllocations(ScopedAllocations);
			}
		};

		PCGEX_SCOPE_LOOP(Index)
		{
			TSharedPtr<PCGExPathfinding::FPathQuery> Query = Queries[Index];

			ON_SCOPE_EXIT
			{
				Query->Cleanup();
			};

			Query->ResolvePicks(Settings->SeedPicking, Settings->GoalPicking);

			if (!Query->HasValidEndpoints())
			{
				continue;
			}

			Query->FindPath(SearchOperation, ScopedAllocations, HeuristicsHandler, nullptr);

			if (!Query->IsQuerySuccessful())
			{
				continue;
			}

			if (bVisited)
			{
				PCGExPathfinding::MarkQueryVisited(*Cluster, *Query, VisitedVtxData, VisitedEdgeData);
			}
			else
			{
				Context->BuildPath(Query, QueriesIO[Query->QueryIndex]);
				QueriesIO[Query->QueryIndex]->IOIndex = EdgeDataFacade->Source->IOIndex * 100000 + Query->QueryIndex;
			}
		}
	}

	void FProcessor::Write()
	{
		// Visited mode only: flush this cluster's per-edge counts. The batch flushes the shared
		// vtx facade (bWriteVtxDataFacade). In Paths mode the write step never runs.
		if (VisitedEdgeData)
		{
			EdgeDataFacade->WriteFastest(TaskManager);
		}
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(PathfindingEdges)

		if (Settings->OutputMode == EPCGExPathfindingOutputMode::Visited && Settings->Statistics.bWritePointUseCount)
		{
			VisitedVtxWriter = VtxDataFacade->GetWritable<int32>(Settings->Statistics.PointUseCountAttributeName, 0, true, Settings->Statistics.bResetValues ? PCGExData::EBufferInit::New : PCGExData::EBufferInit::Inherit);
		}

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}

		PCGEX_TYPED_PROCESSOR

		if (VisitedVtxWriter)
		{
			TypedProcessor->VisitedVtxData = StaticCastSharedPtr<PCGExData::TArrayBuffer<int32>>(VisitedVtxWriter)->GetOutValues()->GetData();
		}

		return true;
	}

}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
