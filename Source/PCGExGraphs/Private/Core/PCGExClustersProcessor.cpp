// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExClustersProcessor.h"

#include "PCGExHeuristicsCommon.h"
#include "Clusters/PCGExClusterDataLibrary.h"
#include "Clusters/PCGExClustersHelpers.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphCommon.h"
#include "Sorting/PCGExSortingRuleProvider.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface


PCGExData::EIOInit UPCGExClustersProcessorSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

PCGExData::EIOInit UPCGExClustersProcessorSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

bool UPCGExClustersProcessorSettings::GetMainAcceptMultipleData() const
{
	return true;
}

bool UPCGExClustersProcessorSettings::WantsScopedIndexLookupBuild() const
{
	PCGEX_GET_OPTION_STATE(ScopedIndexLookupBuild, bDefaultScopedIndexLookupBuild)
}

FPCGExClustersProcessorContext::~FPCGExClustersProcessorContext()
{
	for (TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
	{
		Batch->Cleanup();
		Batch.Reset();
	}

	Batches.Empty();
}

TArray<FPCGPinProperties> UPCGExClustersProcessorSettings::InputPinProperties() const
{
	//TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();

	TArray<FPCGPinProperties> PinProperties;

	if (!IsInputless())
	{
		if (GetMainAcceptMultipleData())
		{
			PCGEX_PIN_POINTS(GetMainInputPin(), "The point data to be processed.", Required)
		}
		else
		{
			PCGEX_PIN_POINT(GetMainInputPin(), "The point data to be processed.", Required)
		}
	}

	PCGEX_PIN_POINTS(PCGExClusters::Labels::SourceEdgesLabel, "Edges associated with the main input points", Required)

	if (SupportsPointFilters())
	{
		if (RequiresPointFilters())
		{
			PCGEX_PIN_FILTERS(GetPointFilterPin(), GetPointFilterTooltip(), Required)
		}
		else
		{
			PCGEX_PIN_FILTERS(GetPointFilterPin(), GetPointFilterTooltip(), Normal)
		}
	}

	if (SupportsEdgeSorting())
	{
		if (RequiresEdgeSorting())
		{
			PCGEX_PIN_FACTORIES(PCGExClusters::Labels::SourceEdgeSortingRules, "Plug sorting rules here. Order is defined by each rule' priority value, in ascending order.", Required, FPCGExDataTypeInfoSortRule::AsId())
		}
		else
		{
			PCGEX_PIN_FACTORIES(PCGExClusters::Labels::SourceEdgeSortingRules, "Plug sorting rules here. Order is defined by each rule' priority value, in ascending order.", Normal, FPCGExDataTypeInfoSortRule::AsId())
		}
	}
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExClustersProcessorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Edges associated with the main output points", Required)
	return PinProperties;
}

bool UPCGExClustersProcessorSettings::SupportsEdgeSorting() const
{
	return false;
}

bool UPCGExClustersProcessorSettings::RequiresEdgeSorting() const
{
	return true;
}

#pragma endregion

const TArray<FPCGExSortRuleConfig>* FPCGExClustersProcessorContext::GetEdgeSortingRules() const
{
	if (EdgeSortingRules.IsEmpty())
	{
		return nullptr;
	}
	return &EdgeSortingRules;
}

bool FPCGExClustersProcessorContext::AdvancePointsIO(const bool bCleanupKeys)
{
	CurrentCluster.Reset();

	CurrentEdgesIndex = -1;

	if (!FPCGExPointsProcessorContext::AdvancePointsIO(bCleanupKeys))
	{
		return false;
	}

	TaggedEdges = ClusterDataLibrary->GetAssociatedEdges(CurrentIO);

	if (TaggedEdges && !TaggedEdges->Entries.IsEmpty())
	{
		PCGExDataId OutId;
		PCGExClusters::Helpers::SetClusterVtx(CurrentIO, OutId); // Update key
		PCGExClusters::Helpers::MarkClusterEdges(TaggedEdges->Entries, OutId);
	}
	else
	{
		TaggedEdges = nullptr;
	}

	if (!TaggedEdges && !bQuietMissingClusterPairElement)
	{
		PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Some input vtx have no associated edges."));
	}

	return true;
}

void FPCGExClustersProcessorContext::OutputBatches() const
{
	for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
	{
		Batch->Output();
	}
}

TSharedPtr<PCGExClusterMT::IBatch> FPCGExClustersProcessorContext::CreateEdgeBatchInstance(const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges) const
{
	return nullptr;
}

bool FPCGExClustersProcessorContext::ProcessClusters(const PCGExCommon::ContextState NextStateId)
{
	if (!bBatchProcessingEnabled)
	{
		return true;
	}

	if (bDaisyChainClusterBatches)
	{
		if (!CurrentBatch)
		{
			// Initialize first batch or end work
			if (CurrentBatchIndex == -1)
			{
				PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), false)
				AdvanceBatch(NextStateId);
				return false;
			}
			return true;
		}

		PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExClusterMT::MTState_ClusterProcessing)
		{
			SetState(PCGExClusterMT::MTState_ClusterCompletingWork);
			if (!CurrentBatch->bSkipCompletion)
			{
				{
					PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), false)
					CurrentBatch->CompleteWork();
				}
				if (IsWaitingForTasks())
				{
					return false;
				}
			}
		}

		PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExClusterMT::MTState_ClusterCompletingWork)
		{
			{
				PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), false)
				AdvanceBatch(NextStateId);
			}
			if (IsWaitingForTasks())
			{
				return false;
			}
		}

		// TODO : We dont support writing step when daisy chaining...?
	}
	else
	{
		PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExClusterMT::MTState_ClusterProcessing)
		{
			ClusterProcessing_InitialProcessingDone();
			SetState(PCGExClusterMT::MTState_ClusterCompletingWork);
			if (!bSkipClusterBatchCompletionStep)
			{
				{
					PCGEX_ASYNC_SCHEDULING_SCOPE(TaskManager, true)
					for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
					{
						Batch->CompleteWork();
					}
				}
				if (IsWaitingForTasks())
				{
					return false;
				}
			}
		}

		PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExClusterMT::MTState_ClusterCompletingWork)
		{
			if (!bSkipClusterBatchCompletionStep)
			{
				ClusterProcessing_WorkComplete();
			}

			if (bDoClusterBatchWritingStep)
			{
				SetState(PCGExClusterMT::MTState_ClusterWriting);
				{
					PCGEX_ASYNC_SCHEDULING_SCOPE(TaskManager, true)
					for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
					{
						Batch->Write();
					}
				}
				if (IsWaitingForTasks())
				{
					return false;
				}
			}
			else
			{
				bBatchProcessingEnabled = false;
				if (NextStateId == PCGExCommon::States::State_Done)
				{
					Done();
				}
				SetState(NextStateId);
				return true;
			}
		}

		PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExClusterMT::MTState_ClusterWriting)
		{
			ClusterProcessing_WritingDone();

			bBatchProcessingEnabled = false;
			if (NextStateId == PCGExCommon::States::State_Done)
			{
				Done();
			}
			SetState(NextStateId);
		}
	}

	return !IsWaitingForTasks();
}

bool FPCGExClustersProcessorContext::CompileGraphBuilders(const bool bOutputToContext, const PCGExCommon::ContextState NextStateId)
{
	PCGEX_ON_STATE_INTERNAL(PCGExGraphs::States::State_ReadyToCompile)
	{
		SetState(PCGExGraphs::States::State_Compiling);
		for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
		{
			Batch->CompileGraphBuilder(bOutputToContext);
		}
	}

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExGraphs::States::State_Compiling)
	{
		ClusterProcessing_GraphCompilationDone();
		SetState(NextStateId);
	}

	return !IsWaitingForTasks();
}

bool FPCGExClustersProcessorContext::StartProcessingClusters(FBatchProcessingValidateEntries&& ValidateEntries, FBatchProcessingInitEdgeBatch&& InitBatch, const bool bDaisyChain)
{
	PCGEX_SETTINGS_LOCAL(ClustersProcessor)

	Batches.Empty();

	bDaisyChainClusterBatches = bDaisyChain;
	CurrentBatchIndex = -1;

	bBatchProcessingEnabled = false;
	bSkipClusterBatchCompletionStep = false;
	bDoClusterBatchWritingStep = false;

	Batches.Reserve(MainPoints->Pairs.Num());

	EdgesDataFacades.Reserve(MainEdges->Pairs.Num());
	for (const TSharedPtr<PCGExData::FPointIO>& EdgeIO : MainEdges->Pairs)
	{
		TSharedPtr<PCGExData::FFacade> EdgeFacade = MakeShared<PCGExData::FFacade>(EdgeIO.ToSharedRef());
		EdgesDataFacades.Add(EdgeFacade.ToSharedRef());
	}

	while (AdvancePointsIO(false))
	{
		if (!TaggedEdges)
		{
			PCGE_LOG_C(Warning, GraphAndLog, this, FTEXT("Some input points have no bound edges."));
			continue;
		}

		if (!ValidateEntries(TaggedEdges))
		{
			continue;
		}

		const TSharedPtr<PCGExClusterMT::IBatch> NewBatch = CreateEdgeBatchInstance(CurrentIO.ToSharedRef(), TaggedEdges->Entries);
		InitBatch(NewBatch);

		if (NewBatch->bRequiresWriteStep)
		{
			bDoClusterBatchWritingStep = true;
		}
		if (NewBatch->bSkipCompletion)
		{
			bSkipClusterBatchCompletionStep = true;
		}
		if (NewBatch->RequiresGraphBuilder())
		{
			NewBatch->GraphBuilderDetails = GraphBuilderDetails;
		}
		if (NewBatch->WantsHeuristics())
		{
			if (!bHasValidHeuristics)
			{
				PCGEX_LOG_MISSING_INPUT(this, FTEXT("Missing Heuristics."))
				return false;
			}
			NewBatch->HeuristicsFactories = &HeuristicsFactories;
		}

		NewBatch->EdgesDataFacades = &EdgesDataFacades;
		Batches.Add(NewBatch);
	}

	if (Batches.IsEmpty())
	{
		return false;
	}

	bBatchProcessingEnabled = true;

	if (!bDaisyChainClusterBatches)
	{
		SetState(PCGExClusterMT::MTState_ClusterProcessing);
		PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), true)

		PCGExMT::ParallelOrSequential(Batches.Num(), [&](const int32 i)
		{
			Batches[i]->PrepareProcessing(GetTaskManager(), bScopedIndexLookupBuild);
		}, /*Threshold=*/2, EParallelForFlags::Unbalanced);
	}

	return true;
}

void FPCGExClustersProcessorContext::ClusterProcessing_InitialProcessingDone()
{
}

void FPCGExClustersProcessorContext::ClusterProcessing_WorkComplete()
{
}

void FPCGExClustersProcessorContext::ClusterProcessing_WritingDone()
{
}

void FPCGExClustersProcessorContext::ClusterProcessing_GraphCompilationDone()
{
}

void FPCGExClustersProcessorContext::AdvanceBatch(const PCGExCommon::ContextState NextStateId)
{
	CurrentBatchIndex++;
	if (!Batches.IsValidIndex(CurrentBatchIndex))
	{
		CurrentBatch = nullptr;
		bBatchProcessingEnabled = false;
		if (NextStateId == PCGExCommon::States::State_Done)
		{
			Done();
		}
		SetState(NextStateId);
	}
	else
	{
		CurrentBatch = Batches[CurrentBatchIndex];
		SetState(PCGExClusterMT::MTState_ClusterProcessing);
		PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(),)
		ScheduleBatch(GetTaskManager(), CurrentBatch, bScopedIndexLookupBuild);
	}
}

void FPCGExClustersProcessorContext::OutputPointsAndEdges() const
{
	MainPoints->StageOutputs();
	MainEdges->StageOutputs();
}

int32 FPCGExClustersProcessorContext::GetClusterProcessorsNum() const
{
	int32 Num = 0;
	for (const TSharedPtr<PCGExClusterMT::IBatch>& Batch : Batches)
	{
		Num += Batch->GetNumProcessors();
	}
	return Num;
}

void FPCGExClustersProcessorElement::DisabledPassThroughData(FPCGContext* Context) const
{
	FPCGExPointsProcessorElement::DisabledPassThroughData(Context);

	//Forward main edges
	TArray<FPCGTaggedData> EdgesSources = Context->InputData.GetInputsByPin(PCGExClusters::Labels::SourceEdgesLabel);
	for (const FPCGTaggedData& TaggedData : EdgesSources)
	{
		FPCGTaggedData& TaggedDataCopy = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedDataCopy.Data = TaggedData.Data;
		TaggedDataCopy.Tags.Append(TaggedData.Tags);
		TaggedDataCopy.Pin = PCGExClusters::Labels::OutputEdgesLabel;
	}
}

bool FPCGExClustersProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ClustersProcessor)

	Context->bQuietMissingClusterPairElement = Settings->bQuietMissingClusterPairElement;

	Context->bHasValidHeuristics = PCGExFactories::GetInputFactories(Context, PCGExHeuristics::Labels::SourceHeuristicsLabel, Context->HeuristicsFactories, {FPCGExDataTypeInfoHeuristics::AsId()}, false);

	Context->ClusterDataLibrary = MakeShared<PCGExClusters::FDataLibrary>(true);

	TArray<TSharedPtr<PCGExData::FPointIO>> TaggedVtx;
	TArray<TSharedPtr<PCGExData::FPointIO>> TaggedEdges;

	Context->MainEdges = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->MainEdges->OutputPin = PCGExClusters::Labels::OutputEdgesLabel;
	TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(PCGExClusters::Labels::SourceEdgesLabel);
	Context->MainEdges->Initialize(Sources);

	if (!Context->ClusterDataLibrary->Build(Context->MainPoints, Context->MainEdges))
	{
		Context->ClusterDataLibrary->PrintLogs(Context);
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Could not find any valid vtx/edge pairs."))
		return false;
	}

	if (Settings->SupportsEdgeSorting())
	{
		Context->EdgeSortingRules = PCGExSorting::GetSortingRules(Context, PCGExClusters::Labels::SourceEdgeSortingRules);
		if (Settings->RequiresEdgeSorting() && Context->EdgeSortingRules.IsEmpty())
		{
			PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing valid sorting rules."))
			return false;
		}
	}

	return true;
}

void FPCGExClustersProcessorElement::InitializeData(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	FPCGExPointsProcessorElement::InitializeData(InContext, InSettings);
	PCGEX_CONTEXT_AND_SETTINGS(ClustersProcessor)

	PCGExData::EIOInit InitMode = Settings->GetEdgeOutputInitMode();
	if (InitMode != PCGExData::EIOInit::NoInit)
	{
		for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainEdges->Pairs)
		{
			IO->InitializeOutput(InitMode);
		}
	}
}

void FPCGExClustersProcessorElement::OnContextInitialized(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorElement::OnContextInitialized(InContext);
	PCGEX_CONTEXT_AND_SETTINGS(ClustersProcessor)
	Context->bScopedIndexLookupBuild = Settings->WantsScopedIndexLookupBuild();
}

#undef LOCTEXT_NAMESPACE
