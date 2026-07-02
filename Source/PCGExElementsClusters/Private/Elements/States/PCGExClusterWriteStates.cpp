// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/States/PCGExClusterWriteStates.h"

#include "Clusters/PCGExCluster.h"
#include "Core/PCGExClusterStates.h"
#include "Core/PCGExPointStates.h"
#include "Data/PCGExData.h"

#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE FlagNodes

PCGExData::EIOInit UPCGExFlagNodesSettings::GetMainOutputInitMode() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGExData::EIOInit UPCGExFlagNodesSettings::GetEdgeOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

#if WITH_EDITOR
void UPCGExFlagNodesSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	InOutNode->RenameInputPin(FName("Node Flags"), PCGExPointStates::Labels::SourceStatesLabel);
	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}
#endif

TArray<FPCGPinProperties> UPCGExFlagNodesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExPointStates::Labels::SourceStatesLabel, "Cluster & Points states.", Required, FPCGExDataTypeInfoPointState::AsId())
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(FlagNodes)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(FlagNodes)

bool FPCGExFlagNodesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(FlagNodes)

	return PCGExFactories::GetInputFactories(Context, PCGExPointStates::Labels::SourceStatesLabel, Context->StateFactories, {FPCGExDataTypeInfoClusterState::AsId()});
}

bool FPCGExFlagNodesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFlagNodesElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FlagNodes)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries)
			{
				return true;
			}, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

namespace PCGExFlagNodes
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindNodeState::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		Cluster->ComputeEdgeLengths();

		StateManager = MakeShared<PCGExClusterStates::FStateManager>(StateFlags, Cluster.ToSharedRef(), VtxDataFacade, EdgeDataFacade);
		StateManager->Init(ExecutionContext, Context->StateFactories);

		StartParallelLoopForNodes();

		return true;
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];

			StateManager->Test(Node);
		}
	}

	//////// BATCH

	FBatch::~FBatch()
	{
		StateFlags = nullptr;
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(FlagNodes)
		PCGExPointFilter::RegisterBuffersDependencies(ExecutionContext, Context->StateFactories, FacadePreloader);
	}

	void FBatch::OnProcessingPreparationComplete()
	{
		PCGEX_TYPED_CONTEXT_AND_SETTINGS(FlagNodes)

		const TSharedPtr<PCGExData::TBuffer<int64>> Writer = VtxDataFacade->GetWritable(Settings->FlagAttribute, Settings->InitialFlags, false, PCGExData::EBufferInit::Inherit);
		const TSharedPtr<PCGExData::TArrayBuffer<int64>> ElementsWriter = StaticCastSharedPtr<PCGExData::TArrayBuffer<int64>>(Writer);
		StateFlags = ElementsWriter->GetOutValues();

		TBatch<FProcessor>::OnProcessingPreparationComplete();
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor))
		{
			return false;
		}
		PCGEX_TYPED_PROCESSOR
		TypedProcessor->StateFlags = StateFlags;
		return true;
	}

	void FBatch::CompleteWork()
	{
		VtxDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
