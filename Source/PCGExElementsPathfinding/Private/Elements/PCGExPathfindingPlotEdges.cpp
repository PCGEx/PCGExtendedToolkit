// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExPathfindingPlotEdges.h"

#include "PCGExHeuristicsHandler.h"
#include "PCGParamData.h"
#include "Algo/Reverse.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Clusters/PCGExCluster.h"
#include "Clusters/PCGExClusterDataLibrary.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Core/PCGExPathQuery.h"
#include "Core/PCGExPlotQuery.h"
#include "Search/PCGExSearchAStar.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Helpers/PCGExTargetsHandler.h"
#include "Paths/PCGExPathsCommon.h"
#include "Paths/PCGExPathsHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExPathfindingPlotEdgesElement"
#define PCGEX_NAMESPACE PathfindingPlotEdges

#if WITH_EDITOR
void UPCGExPathfindingPlotEdgesSettings::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && IsInGameThread())
	{
		if (!SearchAlgorithm) { SearchAlgorithm = NewObject<UPCGExSearchAStar>(this, TEXT("SearchAlgorithm")); }
	}
	Super::PostInitProperties();
}

void UPCGExPathfindingPlotEdgesSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

PCGExData::EIOInit UPCGExPathfindingPlotEdgesSettings::GetMainOutputInitMode() const { return PCGExData::EIOInit::NoInit; }
PCGExData::EIOInit UPCGExPathfindingPlotEdgesSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::NoInit; }

bool UPCGExPathfindingPlotEdgesSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->IsOutputPin() && (InPin->Properties.Label == PCGExMatching::Labels::OutputUnmatchedVtxLabel || InPin->Properties.Label == PCGExMatching::Labels::OutputUnmatchedEdgesLabel))
	{
		return DataMatching.WantsUnmatchedSplit();
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExPathfindingPlotEdgesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::SourcePlotsLabel, "Plot points for pathfinding.", Required)
	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExPathfinding::Labels::SourceOverridesSearch)
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(DataMatching, PinProperties);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExPathfindingPlotEdgesSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPaths::Labels::OutputPathsLabel, "Paths output.", Required)
	PCGExMatching::Helpers::DeclareMatchingRulesOutputs(DataMatching, PinProperties);
	return PinProperties;
}

void FPCGExPathfindingPlotEdgesContext::BuildPath(const TSharedPtr<PCGExPathfinding::FPlotQuery>& Query, const TSharedPtr<PCGExData::FPointIO>& PathIO, const TSharedPtr<PCGExClusters::FClusterDataForwardHandler>& ClusterForwardHandler) const
{
	PCGEX_SETTINGS_LOCAL(PathfindingPlotEdges)

	if (!PathIO) { return; }

	bool bAddGoal = Settings->bAddGoalToPath ? (!Query->bIsClosedLoop || !Settings->bAddSeedToPath) : false;

	int32 NumPoints = Query->SubQueries.Num() + 2;
	int32 ValidPlotIndex = 0;

	int32 MaxQueryNumPoints = 0;
	for (const TSharedPtr<PCGExPathfinding::FPathQuery>& PathQuery : Query->SubQueries)
	{
		if (!PathQuery->IsQuerySuccessful()) { continue; }
		MaxQueryNumPoints = FMath::Max(MaxQueryNumPoints, PathQuery->PathNodes.Num());
		NumPoints += PathQuery->PathNodes.Num();
		ValidPlotIndex++;
	}

	if (ValidPlotIndex == 0) { return; } // No path could be resolved

	//

	TArray<int32> IndicesBuffer;
	IndicesBuffer.Reserve(MaxQueryNumPoints);

	// Create easy-to-track scopes for indices
	PCGExPointArrayDataHelpers::FReadWriteScope PlotScope(ValidPlotIndex + 2, false);
	PCGExPointArrayDataHelpers::FReadWriteScope ClusterScope(NumPoints, false);

	int32 WriteIndex = 0;

	if (Settings->bAddSeedToPath) { PlotScope.Add(Query->SubQueries[0]->Seed.Point.Index, WriteIndex++); }

	for (int i = 0; i < Query->SubQueries.Num(); i++)
	{
		TSharedPtr<PCGExPathfinding::FPathQuery> PathQuery = Query->SubQueries[i];
		if (Settings->bAddPlotPointsToPath && i != 0) { PlotScope.Add(PathQuery->Seed.Point.Index, WriteIndex++); }

		if (!PathQuery->IsQuerySuccessful()) { continue; }

		int32 TruncateStart = 0;
		int32 TruncateEnd = 0;

		// First path, full
		if (Settings->bAddPlotPointsToPath || i == 0) { TruncateStart = TruncateEnd = 0; }
		// Last path, if closed loop, truncated both start & end
		else if (Settings->bClosedLoop && i == Query->SubQueries.Num() - 1) { TruncateStart = TruncateEnd = 1; }
		// Body path, truncated start
		else { TruncateStart = 1; }

		if (Settings->PathComposition == EPCGExPathComposition::Vtx)
		{
			PathQuery->AppendNodePoints(IndicesBuffer, TruncateStart, TruncateEnd);
		}
		else if (Settings->PathComposition == EPCGExPathComposition::Edges)
		{
			PathQuery->AppendEdgePoints(IndicesBuffer);
		}
		else if (Settings->PathComposition == EPCGExPathComposition::VtxAndEdges)
		{
			// TODO : Implement
		}

		ClusterScope.Add(IndicesBuffer, WriteIndex);
		IndicesBuffer.Reset();
	}

	if (bAddGoal) { PlotScope.Add(Query->SubQueries.Last()->Goal.Point.Index, WriteIndex++); }

	if (Settings->PathComposition == EPCGExPathComposition::Vtx)
	{
		if (ClusterScope.Num() < 2) { return; }
	}
	else if (Settings->PathComposition == EPCGExPathComposition::Edges)
	{
		if (ClusterScope.Num() < 1) { return; }
	}
	else if (Settings->PathComposition == EPCGExPathComposition::VtxAndEdges)
	{
		// TODO : Implement
		return;
	}

	if (!Settings->PathOutputDetails.Validate(WriteIndex)) { return; }

	PathIO->Enable();
	PathIO->IOIndex = Query->QueryIndex;

	PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, PathIO.ToSharedRef())
	PCGExPointArrayDataHelpers::SetNumPointsAllocated(PathIO->GetOut(), ClusterScope.Num() + PlotScope.Num(), PathIO->GetAllocations());

	// Commit read/write scopes
	PlotScope.CopyPoints(Query->PlotFacade->GetIn(), PathIO->GetOut(), true, true);
	ClusterScope.CopyProperties(PathIO->GetIn(), PathIO->GetOut(), EPCGPointNativeProperties::All);

	if (TSharedPtr<PCGExData::FDataForwardHandler> PlotForwardHandler = PlotsForwardHandlers[Query->PlotFacade->Idx])
	{
		// TODO: TBD
		//PlotForwardHandler->Forward(0, PathDataFacade);
	}

	if (ClusterForwardHandler)
	{
		// TODO : TBD
	}

	PathIO->Tags->Append(Query->Cluster->EdgesIO.Pin()->Tags.ToSharedRef());
	PathIO->Tags->Append(Query->PlotFacade->Source->Tags.ToSharedRef());

	PCGExClusters::Helpers::CleanupClusterData(PathIO);
	PCGExPaths::Helpers::SetClosedLoop(PathIO->GetOut(), Settings->bClosedLoop);
}

PCGEX_INITIALIZE_ELEMENT(PathfindingPlotEdges)

PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(PathfindingPlotEdges)

bool FPCGExPathfindingPlotEdgesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingPlotEdges)

	PCGEX_FWD(VtxDataForwarding)
	PCGEX_FWD(EdgesDataForwarding)
	Context->VtxDataForwarding.bEnabled = Settings->VtxDataForwarding.bEnabled && Settings->PathComposition == EPCGExPathComposition::Edges;
	Context->EdgesDataForwarding.bEnabled = Settings->EdgesDataForwarding.bEnabled && Settings->PathComposition == EPCGExPathComposition::Vtx;

	PCGEX_BIND_INSTANCED_FACTORY(SearchAlgorithm, UPCGExSearchInstancedFactory, PCGExPathfinding::Labels::SourceOverridesSearch)

	Context->OutputPaths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->OutputPaths->OutputPin = PCGExPaths::Labels::OutputPathsLabel;

	Context->PlotsHandler = MakeShared<PCGExMatching::FTargetsHandler>();
	Context->PlotsHandler->Init(
		Context, PCGExClusters::Labels::SourcePlotsLabel,
		[&](const TSharedPtr<PCGExData::FPointIO>& IO, const int32 Idx)-> FBox
		{
			if (IO->GetNum() < 2)
			{
				if (!Settings->bQuietInvalidPlotWarning) { PCGE_LOG(Warning, GraphAndLog, FTEXT("Pruned plot with < 2 points.")); }
				return FBox(ForceInit);
			}

			return IO->GetIn()->GetBounds();
		});

	Context->NumMaxPlots = Context->PlotsHandler->GetMaxNumTargets();
	if (!Context->NumMaxPlots)
	{
		PCGEX_LOG_MISSING_INPUT(InContext, FTEXT("No targets (no input matches criteria)"))
		return false;
	}

	Context->PlotsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& Target, const int32)
	{
		Context->PlotsForwardHandlers.Add(Target->Idx, Settings->PlotForwarding.TryGetHandler(Target, false));
	});

	if (Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Separated || Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Any || Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Both)
	{
		Context->bMatchForVtx = true;
		Context->bMatchForEdges = true;
	}
	else
	{
		Context->bMatchForVtx = Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Vtx;
		Context->bMatchForEdges = Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Edges;
	}

	if (Context->bMatchForVtx || Context->bMatchForEdges)
	{
		Context->MainDataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
		Context->MainDataMatcher->SetDetails(&Settings->DataMatching);
		if (!Context->MainDataMatcher->Init(Context, Context->PlotsHandler->GetFacades(), true)) { return false; }

		if (Settings->DataMatching.Mode != EPCGExMapMatchMode::Disabled && Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Separated)
		{
			Context->EdgeDataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
			if (!Context->EdgeDataMatcher->Init(Context, Context->MainDataMatcher, PCGExMatching::Labels::SourceMatchRulesEdgesLabel, true)) { return false; }
		}
		else
		{
			Context->EdgeDataMatcher = Context->MainDataMatcher;
		}
	}

	return true;
}

bool FPCGExPathfindingPlotEdgesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPathfindingPlotEdgesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(PathfindingPlotEdges)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters([](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		{
			NewBatch->SetWantsHeuristics(true, Settings->HeuristicScoreMode);
		}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPaths->StageOutputs();
	if (Settings->DataMatching.WantsUnmatchedSplit()) { Context->OutputPointsAndEdges(); }

	return Context->TryComplete();
}

namespace PCGExPathfindingPlotEdges
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPathfindingPlotEdges::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		if (Context->bMatchForEdges)
		{
			PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true);
			Context->EdgeDataMatcher->PopulateIgnoreList(EdgeDataFacade->Source->GetTaggedData(), MatchingScope, IgnoreList);
		}

		if (VtxIgnoreList)
		{
			if (Settings->DataMatching.ClusterMatchMode == EPCGExClusterComponentTagMatchMode::Any) { IgnoreList = IgnoreList.Intersect(*VtxIgnoreList); }
			else { if (Context->bMatchForVtx) { IgnoreList.Append(*VtxIgnoreList); } }
		}

		ValidPlots.Reserve(Context->PlotsHandler->Num() - IgnoreList.Num());
		Context->PlotsHandler->ForEachTarget([&](const TSharedRef<PCGExData::FFacade>& Target, const int32 i) { ValidPlots.Add(Target); }, &IgnoreList);

		if (ValidPlots.IsEmpty()) { return false; }

		ClusterDataForwardHandler = MakeShared<PCGExClusters::FClusterDataForwardHandler>(Cluster, StaticCastSharedPtr<FBatch>(ParentBatch.Pin())->VtxDataForwardHandler, Context->EdgesDataForwarding.TryGetHandler(EdgeDataFacade, false));

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

		TSharedPtr<PCGExData::FPointIO> ReferenceIO = nullptr;

		if (Settings->PathComposition == EPCGExPathComposition::Vtx) { ReferenceIO = VtxDataFacade->Source; }
		else if (Settings->PathComposition == EPCGExPathComposition::Edges) { ReferenceIO = EdgeDataFacade->Source; }
		else if (Settings->PathComposition == EPCGExPathComposition::VtxAndEdges)
		{
			// TODO : Implement
		}

		SearchOperation = Context->SearchAlgorithm->CreateOperation(); // Create a local copy
		SearchOperation->PrepareForCluster(Cluster.Get());
		const int32 NumPlots = ValidPlots.Num();
		PCGExArrayHelpers::InitArray(Queries, NumPlots);
		QueriesIO.Init(nullptr, NumPlots);

		Context->OutputPaths->IncreaseReserve(NumPlots);
		for (int i = 0; i < NumPlots; i++)
		{
			PCGEX_MAKE_SHARED(Query, PCGExPathfinding::FPlotQuery, Cluster.ToSharedRef(), Settings->bClosedLoop, i)
			Queries[i] = Query;
			QueriesIO[i] = Context->OutputPaths->Emplace_GetRef<UPCGPointArrayData>(ReferenceIO, PCGExData::EIOInit::New);
			QueriesIO[i]->Disable();
		}

		bForceSingleThreadedProcessRange = HeuristicsHandler->HasGlobalFeedback() || !Settings->bGreedyQueries;
		if (bForceSingleThreadedProcessRange) { SearchAllocations = SearchOperation->NewAllocations(); }

		// Build all queries first
		for (int i = 0; i < NumPlots; i++)
		{
			Queries[i]->BuildPlotQuery(ValidPlots[i], Settings->SeedPicking, Settings->GoalPicking);
		}

		if (bForceSingleThreadedProcessRange)
		{
			// Chain FindPaths calls through OnCompleteCallback to ensure sequential execution with shared allocations
			for (int i = 0; i < NumPlots; i++)
			{
				const TSharedPtr<PCGExPathfinding::FPlotQuery>& Query = Queries[i];
				const int32 NextIndex = i + 1;

				Query->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE, NextIndex](const TSharedPtr<PCGExPathfinding::FPlotQuery>& Plot)
				{
					PCGEX_ASYNC_THIS
					This->Context->BuildPath(Plot, This->QueriesIO[Plot->QueryIndex]);
					This->QueriesIO[Plot->QueryIndex]->IOIndex = This->EdgeDataFacade->Source->IOIndex * 100000 + Plot->QueryIndex;
					Plot->Cleanup();

					// Start next query if there is one
					if (NextIndex < This->Queries.Num())
					{
						This->Queries[NextIndex]->FindPaths(This->TaskManager, This->SearchOperation, This->SearchAllocations, This->HeuristicsHandler);
					}
				};
			}

			// Start the first query
			Queries[0]->FindPaths(TaskManager, SearchOperation, SearchAllocations, HeuristicsHandler);
		}
		else
		{
			// Parallel execution - each query gets its own allocations
			StartParallelLoopForRange(Queries.Num(), 1);
		}

		return true;
	}

	void FProcessor::ProcessRange(const PCGExMT::FScope& Scope)
	{
		PCGEX_SCOPE_LOOP(Index)
		{
			TSharedPtr<PCGExPathfinding::FPlotQuery> Query = Queries[Index];
			Query->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE](const TSharedPtr<PCGExPathfinding::FPlotQuery>& Plot)
			{
				PCGEX_ASYNC_THIS
				This->Context->BuildPath(Plot, This->QueriesIO[Plot->QueryIndex]);
				This->QueriesIO[Plot->QueryIndex]->IOIndex = This->EdgeDataFacade->Source->IOIndex * 100000 + Plot->QueryIndex;
				Plot->Cleanup();
			};
			Query->FindPaths(TaskManager, SearchOperation, SearchAllocations, HeuristicsHandler);
		}
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExPathfindingPlotEdgesContext, UPCGExPathfindingPlotEdgesSettings>::Cleanup();
		ClusterDataForwardHandler.Reset();
	}

	void FBatch::Process()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(PathfindingPlotEdges)

		VtxDataForwardHandler = Context->VtxDataForwarding.TryGetHandler(VtxDataFacade, false);

		if (PCGExMatching::FScope MatchingScope(Context->InitialMainPointsNum, true);
			Context->bMatchForVtx && !Context->MainDataMatcher->PopulateIgnoreList(VtxDataFacade->Source->GetTaggedData(), MatchingScope, IgnoreList))
		{
			bUnmatched = true;
		}

		TBatch<FProcessor>::Process();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor)) { return false; }
		PCGEX_TYPED_PROCESSOR
		TypedProcessor->VtxIgnoreList = &IgnoreList;
		return true;
	}

	void FBatch::CompleteWork()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(PathfindingPlotEdges)

		if (Settings->DataMatching.WantsUnmatchedSplit())
		{
			int32 NumEdgesMatched = 0;
			for (const TSharedRef<PCGExClusterMT::IProcessor>& InProcessor : Processors)
			{
				PCGEX_TYPED_PROCESSOR_NREF(P)
				if (!P->ValidPlots.IsEmpty()) { NumEdgesMatched++; }
				else
				{
					P->EdgeDataFacade->Source->OutputPin = PCGExMatching::Labels::OutputUnmatchedEdgesLabel;
					P->EdgeDataFacade->Source->InitializeOutput(PCGExData::EIOInit::Forward);
				}
			}

			if (NumEdgesMatched != Processors.Num())
			{
				VtxDataFacade->Source->OutputPin = PCGExMatching::Labels::OutputUnmatchedVtxLabel;
				VtxDataFacade->Source->InitializeOutput(PCGExData::EIOInit::Forward);
			}
		}

		TBatch<FProcessor>::CompleteWork();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
