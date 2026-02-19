// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExRefineEdges.h"


#include "PCGParamData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Graphs/PCGExGraph.h"
#include "Refinements/PCGExEdgeRefinePrimMST.h"
#include "Core/PCGExClusterFilter.h"
#include "Async/ParallelFor.h"
#include "PCGExVersion.h"
#include "Graphs/PCGExGraphBuilder.h"

#define LOCTEXT_NAMESPACE "PCGExRefineEdges"
#define PCGEX_NAMESPACE RefineEdges

#if WITH_EDITOR
void UPCGExRefineEdgesSettings::ApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_UPDATE_TO_DATA_VERSION(1, 70, 11)
	{
		if (bOutputEdgesOnly_DEPRECATED) { Mode = EPCGExRefineEdgesOutput::Points; }
	}

	PCGEX_UPDATE_TO_DATA_VERSION(1, 71, 2)
	{
		ResultOutputVtx.ApplyDeprecation();
		ResultOutputEdges.ApplyDeprecation();
	}

	Super::ApplyDeprecation(InOutNode);
}
#endif

bool UPCGExRefineEdgesSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExHeuristics::Labels::SourceHeuristicsLabel) { return Refinement && Refinement->WantsHeuristics(); }
	if (InPin->Properties.Label == PCGExClusters::Labels::SourceEdgeFiltersLabel) { return Refinement && Refinement->SupportFilters(); }

	return Super::IsPinUsedByNodeExecution(InPin);
}

TArray<FPCGPinProperties> UPCGExRefineEdgesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	if (Refinement && Refinement->WantsHeuristics()) { PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics may be required by some refinements.", Required, FPCGExDataTypeInfoHeuristics::AsId()) }
	else { PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics may be required by some refinements.", Advanced, FPCGExDataTypeInfoHeuristics::AsId()) }

	if (Refinement && Refinement->SupportFilters()) { PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceEdgeFiltersLabel, "Refinements filters.", Normal) }
	else { PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceEdgeFiltersLabel, "Refinements filters.", Advanced) }

	if (Sanitization == EPCGExRefineSanitization::Filters)
	{
		PCGEX_PIN_FILTERS(PCGExRefineEdges::SourceSanitizeEdgeFilters, "Filters that define which edges are to be kept. During the sanitization step, edges that pass the filters are restored if they were previously removed.", Required)
	}

	PCGEX_PIN_OPERATION_OVERRIDES(PCGExRefineEdges::SourceOverridesRefinement)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExRefineEdgesSettings::OutputPinProperties() const
{
	if (Mode != EPCGExRefineEdgesOutput::Points) { return Super::OutputPinProperties(); }

	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputKeptEdgesLabel, "Kept edges but as simple points.", Required)
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputRemovedEdgesLabel, "Removed edges but as simple points.", Required)
	return PinProperties;
}

PCGExData::EIOInit UPCGExRefineEdgesSettings::GetMainOutputInitMode() const
{
	switch (Mode)
	{
	default: case EPCGExRefineEdgesOutput::Clusters: return PCGExData::EIOInit::New;
	case EPCGExRefineEdgesOutput::Points: return PCGExData::EIOInit::NoInit;
	case EPCGExRefineEdgesOutput::Attribute: return PCGExData::EIOInit::Duplicate;
	}
}

PCGExData::EIOInit UPCGExRefineEdgesSettings::GetEdgeOutputInitMode() const
{
	return Mode == EPCGExRefineEdgesOutput::Attribute ? PCGExData::EIOInit::Duplicate : PCGExData::EIOInit::NoInit;
}

PCGEX_INITIALIZE_ELEMENT(RefineEdges)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(RefineEdges)

bool FPCGExRefineEdgesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(RefineEdges)

	if (!Settings->Refinement)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("No refinement selected."));
		return false;
	}

	PCGEX_BIND_INSTANCED_FACTORY(Refinement, UPCGExEdgeRefineInstancedFactory, PCGExRefineEdges::SourceOverridesRefinement)
	PCGEX_FWD(GraphBuilderDetails)

	if (Context->Refinement->WantsHeuristics() && !Context->bHasValidHeuristics)
	{
		PCGE_LOG(Error, GraphAndLog, FTEXT("The selected refinement requires heuristics to be connected, but none can be found."));
		return false;
	}

	if (Context->Refinement->SupportFilters())
	{
		//GetInputFactories(Context, PCGExRefineEdges::SourceVtxFilters, Context->VtxFilterFactories, PCGExFactories::ClusterNodeFilters, false);
		GetInputFactories(Context, PCGExClusters::Labels::SourceEdgeFiltersLabel, Context->EdgeFilterFactories, PCGExFactories::ClusterEdgeFilters, false);
	}

	if (Settings->Sanitization == EPCGExRefineSanitization::Filters)
	{
		if (!GetInputFactories(Context, PCGExRefineEdges::SourceSanitizeEdgeFilters, Context->SanitizationFilterFactories, PCGExFactories::ClusterEdgeFilters))
		{
			return false;
		}
	}

	if (Settings->Mode == EPCGExRefineEdgesOutput::Points)
	{
		// TODO : Revisit this

		Context->KeptEdges = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->KeptEdges->OutputPin = PCGExClusters::Labels::OutputKeptEdgesLabel;

		Context->RemovedEdges = MakeShared<PCGExData::FPointIOCollection>(Context);
		Context->RemovedEdges->OutputPin = PCGExClusters::Labels::OutputRemovedEdgesLabel;

		int32 NumEdgesInputs = Context->MainEdges->Num();
		Context->KeptEdges->Pairs.Reserve(NumEdgesInputs);
		Context->RemovedEdges->Pairs.Reserve(NumEdgesInputs);

		for (const TSharedPtr<PCGExData::FPointIO>& EdgeIO : Context->MainEdges->Pairs)
		{
			Context->KeptEdges->Emplace_GetRef(EdgeIO, PCGExData::EIOInit::New)->bAllowEmptyOutput = Settings->bAllowZeroPointOutputs;
			Context->RemovedEdges->Emplace_GetRef(EdgeIO, PCGExData::EIOInit::New)->bAllowEmptyOutput = Settings->bAllowZeroPointOutputs;
		}
	}
	else if (Settings->Mode == EPCGExRefineEdgesOutput::Attribute)
	{
		if (!Settings->ResultOutputVtx.Validate(Context)) { return false; }
		if (!Settings->ResultOutputEdges.Validate(Context)) { return false; }
	}

	return true;
}

bool FPCGExRefineEdgesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExRefineEdgesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(RefineEdges)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters([](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		{
			NewBatch->GraphBuilderDetails = Context->GraphBuilderDetails;
			if (Context->Refinement->WantsHeuristics()) { NewBatch->SetWantsHeuristics(true, Settings->HeuristicScoreMode); }
			NewBatch->bRequiresWriteStep = Settings->Mode == EPCGExRefineEdgesOutput::Attribute;
		}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(Settings->Mode != EPCGExRefineEdgesOutput::Clusters ? PCGExCommon::States::State_Done : PCGExGraphs::States::State_ReadyToCompile)

	if (Settings->Mode == EPCGExRefineEdgesOutput::Clusters)
	{
		// Wait for compilation
		if (!Context->CompileGraphBuilders(true, PCGExCommon::States::State_Done)) { return false; }
		Context->MainPoints->StageOutputs();
	}
	else if (Settings->Mode == EPCGExRefineEdgesOutput::Points)
	{
		Context->KeptEdges->StageOutputs();
		Context->RemovedEdges->StageOutputs();
	}
	else if (Settings->Mode == EPCGExRefineEdgesOutput::Attribute)
	{
		Context->OutputPointsAndEdges();
	}

	return Context->TryComplete();
}

namespace PCGExRefineEdges
{
	class FSanitizeRangeTask final : public PCGExMT::FScopeIterationTask
	{
	public:
		explicit FSanitizeRangeTask(const TSharedPtr<FProcessor>& InProcessor)
			: FScopeIterationTask(), Processor(InProcessor)
		{
		}

		TSharedPtr<FProcessor> Processor;

		virtual void ExecuteTask(const TSharedPtr<PCGExMT::FTaskManager>& TaskManager) override
		{
			auto RestoreEdge = [&](const int32 EdgeIndex)
			{
				if (EdgeIndex == -1) { return; }
				FPlatformAtomics::InterlockedExchange(&Processor->Cluster->GetEdge(EdgeIndex)->bValid, 1);
				FPlatformAtomics::InterlockedExchange(&Processor->Cluster->GetEdgeStart(EdgeIndex)->bValid, 1);
				FPlatformAtomics::InterlockedExchange(&Processor->Cluster->GetEdgeEnd(EdgeIndex)->bValid, 1);
			};

			if (Processor->Sanitization == EPCGExRefineSanitization::Longest)
			{
				PCGEX_SCOPE_LOOP(i)
				{
					const PCGExClusters::FNode* Node = (Processor->Cluster->GetNode(i));

					int32 BestIndex = -1;
					double LongestDist = 0;

					for (const PCGExGraphs::FLink Lk : Node->Links)
					{
						const double Dist = Processor->Cluster->GetDistSquared(Node->Index, Lk.Node);
						if (Dist > LongestDist)
						{
							LongestDist = Dist;
							BestIndex = Lk.Edge;
						}
					}

					RestoreEdge(BestIndex);
				}
			}
			else if (Processor->Sanitization == EPCGExRefineSanitization::Shortest)
			{
				PCGEX_SCOPE_LOOP(i)
				{
					const PCGExClusters::FNode* Node = Processor->Cluster->GetNode(i);

					int32 BestIndex = -1;
					double ShortestDist = MAX_dbl;

					for (const PCGExGraphs::FLink Lk : Node->Links)
					{
						const double Dist = Processor->Cluster->GetDistSquared(Node->Index, Lk.Node);
						if (Dist < ShortestDist)
						{
							ShortestDist = Dist;
							BestIndex = Lk.Edge;
						}
					}

					RestoreEdge(BestIndex);
				}
			}
		}
	};

	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		// Create a light working copy with edges only, will be deleted.
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, EdgeDataFacade->Source, NodeIndexLookup, false, true, false);
	}

	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExRefineEdges::Process);

		EdgeFilterFactories = &Context->EdgeFilterFactories; // So filters can be initialized

		if (!IProcessor::Process(InTaskManager)) { return false; }

		Sanitization = Settings->Sanitization;

		Refinement = Context->Refinement->CreateOperation();
		if (!Refinement) { return false; }

		Refinement->PrimaryDataFacade = VtxDataFacade;
		Refinement->SecondaryDataFacade = EdgeDataFacade;

		Refinement->PrepareForCluster(Cluster, HeuristicsHandler);

		Refinement->VtxFilterCache = VtxFilterCache.Get();
		Refinement->EdgeFilterCache = &EdgeFilterCache;

		const int32 PLI = PCGEX_CORE_SETTINGS.GetClusterBatchChunkSize();

		if (Settings->Mode == EPCGExRefineEdgesOutput::Attribute)
		{
			ResultOutputVtx = StaticCastSharedPtr<FBatch>(ParentBatch.Pin())->ResultOutputVtx;

			ResultOutputEdges = Settings->ResultOutputEdges;
			if (ResultOutputEdges.bEnabled) { ResultOutputEdges.Init(EdgeDataFacade); }
		}

		if (Settings->Sanitization == EPCGExRefineSanitization::Filters)
		{
			if (!Context->SanitizationFilterFactories.IsEmpty())
			{
				SanitizationFilterManager = MakeShared<PCGExClusterFilter::FManager>(Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
				SanitizationFilterManager->bUseEdgeAsPrimary = true;
				SanitizationFilterManager->SetSupportedTypes(&PCGExFactories::ClusterEdgeFilters);
				if (!SanitizationFilterManager->Init(ExecutionContext, Context->SanitizationFilterFactories)) { return false; }
			}
		}

		// Need to go through PrepareSingleLoopScopeForEdges anyway

		if (Context->Refinement->WantsIndividualEdgeProcessing())
		{
			StartParallelLoopForEdges();
		}
		else
		{
			PCGEX_ASYNC_GROUP_CHKD(TaskManager, EdgeScopeLoop)

			EdgeScopeLoop->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				if (This->Context->Refinement->WantsIndividualNodeProcessing()) { This->StartParallelLoopForNodes(); }
				else { This->Refinement->Process(); }
			};

			EdgeScopeLoop->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				This->PrepareSingleLoopScopeForEdges(Scope);
			};

			EdgeScopeLoop->StartSubLoops(EdgeDataFacade->GetNum(), PLI);
		}

		return true;
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		PCGEX_SCOPE_LOOP(Index)
		{
			Refinement->ProcessNode(Nodes[Index]);
		}
	}

	void FProcessor::PrepareSingleLoopScopeForEdges(const PCGExMT::FScope& Scope)
	{
		EdgeDataFacade->Fetch(Scope);
		FilterEdgeScope(Scope);

		TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;

		const bool bDefaultValidity = Context->Refinement->GetDefaultEdgeValidity();
		PCGEX_SCOPE_LOOP(i) { Edges[i].bValid = bDefaultValidity; }
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		PrepareSingleLoopScopeForEdges(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraphs::FEdge& Edge = *Cluster->GetEdge(Index);
			Refinement->ProcessEdge(Edge);
		}
	}

	void FProcessor::OnEdgesProcessingComplete()
	{
		if (!Settings->bRestoreEdgesThatConnectToValidNodes) { return; }

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, InvalidateNodes)

		InvalidateNodes->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			const PCGExClusters::FCluster* LocalCluster = This->Cluster.Get();
			PCGEX_SCOPE_LOOP(i)
			{
				if (PCGExClusters::FNode* Node = LocalCluster->GetNode(i); !Node->HasAnyValidEdges(LocalCluster)) { Node->bValid = false; }
			}
		};

		InvalidateNodes->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			PCGEX_ASYNC_GROUP_CHKD_VOID(This->TaskManager, RestoreEdges)
			RestoreEdges->OnSubLoopStartCallback = [AsyncThis](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_NESTED_THIS
				const PCGExClusters::FCluster* LocalCluster = NestedThis->Cluster.Get();

				PCGEX_SCOPE_LOOP(i)
				{
					PCGExGraphs::FEdge* Edge = LocalCluster->GetEdge(i);
					if (Edge->bValid) { continue; }
					if (LocalCluster->GetEdgeStart(i)->bValid && LocalCluster->GetEdgeEnd(i)->bValid) { Edge->bValid = true; }
				}
			};

			RestoreEdges->StartSubLoops(This->Cluster->Edges->Num(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
		};


		InvalidateNodes->StartSubLoops(Cluster->Nodes->Num(), PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
	}

	void FProcessor::Sanitize()
	{
		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, SanitizeTaskGroup)

		Cluster->GetBoundedEdges(true); //Oof

		SanitizeTaskGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->InsertEdges();
		};

		if (Settings->Sanitization == EPCGExRefineSanitization::Filters)
		{
			const int32 PLI = PCGEX_CORE_SETTINGS.GetClusterBatchChunkSize();
			SanitizeTaskGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS

				const TSharedPtr<PCGExClusters::FCluster> LocalCluster = This->Cluster;
				const TSharedPtr<PCGExClusterFilter::FManager> SanitizationFilters = This->SanitizationFilterManager;

				PCGEX_SCOPE_LOOP(i)
				{
					PCGExGraphs::FEdge& Edge = *LocalCluster->GetEdge(i);
					if (SanitizationFilters->Test(Edge)) { Edge.bValid = true; }
				}
			};
			SanitizeTaskGroup->StartSubLoops(EdgeDataFacade->GetNum(), PLI);
		}
		else
		{
			PCGEX_SHARED_THIS_DECL
			SanitizeTaskGroup->StartRanges<FSanitizeRangeTask>(NumNodes, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize(), false, ThisPtr);
		}
	}

	void FProcessor::InsertEdges() const
	{
		if (Settings->Mode == EPCGExRefineEdgesOutput::Attribute)
		{
			// TODO : Re-validate all edges & nodes
			TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes.Get();
			TArray<PCGExClusters::FEdge>& Edges = *Cluster->Edges.Get();

			if (ResultOutputVtx.bEnabled)
			{
				if (Nodes.Num() > 1024)
				{
					ParallelFor(Nodes.Num(), [&](const int32 i)
					{
						PCGExClusters::FNode& Node = Nodes[i];
						if (Node.bValid)
						{
							int32 ValidCount = 0;
							for (const PCGExGraphs::FLink& Lk : Node.Links) { ValidCount += Edges[Lk.Edge].bValid; }
							ResultOutputVtx.Write(Node.PointIndex, static_cast<bool>(ValidCount));
						}
						else
						{
							ResultOutputVtx.Write(Node.PointIndex, static_cast<bool>(Node.bValid));
							Node.bValid = true;
						}
					});
				}
				else
				{
					for (PCGExClusters::FNode& Node : Nodes)
					{
						if (Node.bValid)
						{
							int32 ValidCount = 0;
							for (const PCGExGraphs::FLink& Lk : Node.Links) { ValidCount += Edges[Lk.Edge].bValid; }
							ResultOutputVtx.Write(Node.PointIndex, static_cast<bool>(ValidCount));
						}
						else
						{
							ResultOutputVtx.Write(Node.PointIndex, static_cast<bool>(Node.bValid));
							Node.bValid = true;
						}
					}
				}
			}

			if (ResultOutputEdges.bEnabled)
			{
				for (PCGExClusters::FEdge& Edge : Edges)
				{
					ResultOutputEdges.Write(Edge.Index, static_cast<bool>(Edge.bValid));
					Edge.bValid = true;
				}
			}


			EdgeDataFacade->WriteFastest(TaskManager);
		}
		else if (Settings->Mode == EPCGExRefineEdgesOutput::Points)
		{
			const UPCGBasePointData* OriginalEdges = EdgeDataFacade->GetIn();

			TBitArray<> Mask;
			Mask.Init(false, OriginalEdges->GetNumPoints());

			const TArray<PCGExGraphs::FEdge>& Edges = *Cluster->Edges;
			for (int i = 0; i < Mask.Num(); ++i)
			{
				Mask[i] = Edges[i].bValid ? true : false;
			}

			(void)Context->KeptEdges->Pairs[EdgeDataFacade->Source->IOIndex]->InheritPoints(Mask, false);
			(void)Context->RemovedEdges->Pairs[EdgeDataFacade->Source->IOIndex]->InheritPoints(Mask, true);
		}
		else if (Settings->Mode == EPCGExRefineEdgesOutput::Clusters)
		{
			if (!GraphBuilder) { return; }

			TArray<PCGExGraphs::FEdge> ValidEdges;
			Cluster->GetValidEdges(ValidEdges);

			if (ValidEdges.IsEmpty()) { return; }

			GraphBuilder->Graph->InsertEdges(ValidEdges);
		}
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->Sanitization != EPCGExRefineSanitization::None)
		{
			Sanitize();
			return;
		}

		InsertEdges();
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExRefineEdgesContext, UPCGExRefineEdgesSettings>::Cleanup();
		Refinement.Reset();
		SanitizationFilterManager.Reset();
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(RefineEdges)

		if (Settings->Mode == EPCGExRefineEdgesOutput::Attribute)
		{
			ResultOutputVtx = Settings->ResultOutputVtx;
			if (ResultOutputVtx.bEnabled) { ResultOutputVtx.Init(VtxDataFacade); }
		}

		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);

		Context->Refinement->RegisterBuffersDependencies(ExecutionContext, FacadePreloader);

		//PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->VtxFilterFactories, FacadePreloader);
		PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->EdgeFilterFactories, FacadePreloader);
		PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->SanitizationFilterFactories, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(RefineEdges)

		Context->Refinement->PrepareVtxFacade(VtxDataFacade);
		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	void FBatch::Write()
	{
		VtxDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
