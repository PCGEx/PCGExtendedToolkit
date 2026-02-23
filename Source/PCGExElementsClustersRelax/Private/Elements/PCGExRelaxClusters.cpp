// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExRelaxClusters.h"


#include "PCGParamData.h"
#include "Containers/PCGExScopedContainers.h"
#include "Data/PCGExData.h"
#include "Core/PCGExRelaxClusterOperation.h"
#include "Core/PCGExPointFilter.h"
#include "Details/PCGExInfluenceDetails.h"
#include "Helpers/PCGExMetaHelpers.h"
#include "Types/PCGExTypeOpsRotation.h"

#define LOCTEXT_NAMESPACE "PCGExRelaxClusters"
#define PCGEX_NAMESPACE RelaxClusters

PCGExData::EIOInit UPCGExRelaxClustersSettings::GetMainOutputInitMode() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }
PCGExData::EIOInit UPCGExRelaxClustersSettings::GetEdgeOutputInitMode() const { return StealData == EPCGExOptionState::Enabled ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate; }

TArray<FPCGPinProperties> UPCGExRelaxClustersSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceVtxFiltersLabel, "Vtx filters.", Normal)
	PCGEX_PIN_OPERATION_OVERRIDES(PCGExRelaxClusters::SourceOverridesRelaxing)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(RelaxClusters)
PCGEX_ELEMENT_BATCH_EDGE_IMPL_ADV(RelaxClusters)

bool FPCGExRelaxClustersElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExClustersProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(RelaxClusters)
	PCGEX_FOREACH_FIELD_RELAX_CLUSTER(PCGEX_OUTPUT_VALIDATE_NAME)
	PCGEX_BIND_INSTANCED_FACTORY(Relaxing, UPCGExRelaxClusterOperation, PCGExRelaxClusters::SourceOverridesRelaxing)

	GetInputFactories(Context, PCGExClusters::Labels::SourceVtxFiltersLabel, Context->VtxFilterFactories, PCGExFactories::ClusterNodeFilters, false);

	return true;
}

bool FPCGExRelaxClustersElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExRelaxClustersElement::Execute);

	// TODO : Support filters to set influence to 0 on those.

	PCGEX_CONTEXT_AND_SETTINGS(RelaxClusters)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters([](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; }, [&](const TSharedPtr<PCGExClusterMT::IBatch>& NewBatch)
		{
			NewBatch->bRequiresWriteStep = true;
			NewBatch->AllocateVtxProperties = EPCGPointNativeProperties::Transform;
			NewBatch->VtxFilterFactories = &Context->VtxFilterFactories;
		}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->OutputPointsAndEdges();

	return Context->TryComplete();
}

namespace PCGExRelaxClusters
{
	FProcessor::~FProcessor()
	{
	}

	TSharedPtr<PCGExClusters::FCluster> FProcessor::HandleCachedCluster(const TSharedRef<PCGExClusters::FCluster>& InClusterRef)
	{
		return MakeShared<PCGExClusters::FCluster>(InClusterRef, VtxDataFacade->Source, VtxDataFacade->Source, NodeIndexLookup, true, false, false);
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExRelaxClusters::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		InfluenceDetails = Settings->InfluenceDetails;
		if (!InfluenceDetails.Init(ExecutionContext, VtxDataFacade)) { return false; }

		RelaxOperation = Cast<UPCGExRelaxClusterOperation>(Context->Relaxing->CreateNewInstance(Context->ManagedObjects.Get()));
		if (!RelaxOperation) { return false; }

		RelaxOperation->PrimaryDataFacade = VtxDataFacade;
		RelaxOperation->SecondaryDataFacade = EdgeDataFacade;


		PrimaryBuffer = MakeShared<TArray<FTransform>>();
		SecondaryBuffer = MakeShared<TArray<FTransform>>();

		PrimaryBuffer->Init(FTransform::Identity, NumNodes);
		SecondaryBuffer->Init(FTransform::Identity, NumNodes);

		TArray<FTransform>& PBufferRef = (*PrimaryBuffer);
		TArray<FTransform>& SBufferRef = (*SecondaryBuffer);

		TArray<PCGExClusters::FNode> NodesRef = *Cluster->Nodes.Get();
		TConstPCGValueRange<FTransform> InTransforms = VtxDataFacade->GetIn()->GetConstTransformValueRange();

		for (int i = 0; i < NumNodes; i++) { PBufferRef[i] = SBufferRef[i] = InTransforms[NodesRef[i].PointIndex]; }

		RelaxOperation->ReadBuffer = PrimaryBuffer.Get();
		RelaxOperation->WriteBuffer = SecondaryBuffer.Get();

		if (!RelaxOperation->PrepareForCluster(ExecutionContext, Cluster)) { return false; }

		Iterations = Settings->Iterations;

		Steps = RelaxOperation->GetNumSteps();
		CurrentStep = -1;

		if (VtxFiltersManager)
		{
			PCGEX_ASYNC_GROUP_CHKD(TaskManager, VtxTesting)

			VtxTesting->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->StartNextStep();
			};

			VtxTesting->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
			{
				PCGEX_ASYNC_THIS
				This->FilterVtxScope(Scope);
			};

			VtxTesting->StartSubLoops(NumNodes, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());
		}
		else
		{
			StartNextStep();
		}

		return true;
	}

	void FProcessor::StartNextStep()
	{
		CurrentStep++;

		if (Iterations <= 0)
		{
			// Wrap up
			StartParallelLoopForNodes();
			return;
		}

		if (CurrentStep > Steps)
		{
			Iterations--;
			CurrentStep = 0;
		}

		StepSource = RelaxOperation->PrepareNextStep(CurrentStep);

		PCGEX_ASYNC_GROUP_CHKD_VOID(TaskManager, IterationGroup)

		IterationGroup->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->StartNextStep();
		};

		IterationGroup->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->RelaxScope(Scope);
		};

		switch (StepSource)
		{
		case EPCGExClusterElement::Vtx: IterationGroup->StartSubLoops(NumNodes, 32);
			break;
		case EPCGExClusterElement::Edge: IterationGroup->StartSubLoops(NumEdges, 32);
			break;
		}
	}

	void FProcessor::RelaxScope(const PCGExMT::FScope& Scope) const
	{
		const TArray<FTransform>& RBufferRef = (*RelaxOperation->ReadBuffer);
		TArray<FTransform>& WBufferRef = (*RelaxOperation->WriteBuffer);

#define PCGEX_RELAX_PROGRESS  WBufferRef[i] = PCGExTypeOps::FTypeOps<FTransform>::Lerp( RBufferRef[i], WBufferRef[i], InfluenceDetails.GetInfluence(Node.PointIndex));
#define PCGEX_RELAX_FILTER if(!IsNodePassingFilters(Node)){ WBufferRef[i] = RBufferRef[i]; }else
#define PCGEX_RELAX_STEP_NODE(_STEP) if (CurrentStep == _STEP-1){\
		if(bLastStep){ \
			if(InfluenceDetails.bProgressiveInfluence){PCGEX_SCOPE_LOOP(i){ PCGExClusters::FNode& Node = *Cluster->GetNode(i); RelaxOperation->Step##_STEP(Node); PCGEX_RELAX_FILTER{ PCGEX_RELAX_PROGRESS }} } \
			else{ PCGEX_SCOPE_LOOP(i){ PCGExClusters::FNode& Node = *Cluster->GetNode(i); RelaxOperation->Step##_STEP(Node); PCGEX_RELAX_FILTER{} } } \
		}else{ \
			PCGEX_SCOPE_LOOP(i){ RelaxOperation->Step##_STEP(*Cluster->GetNode(i)); \
		}} return; }

#define PCGEX_RELAX_STEP_EDGE(_STEP) if (CurrentStep == _STEP-1){ PCGEX_SCOPE_LOOP(i){ RelaxOperation->Step##_STEP(*Cluster->GetEdge(i)); } return; }

		const bool bLastStep = (CurrentStep == (Steps - 1));

		switch (StepSource)
		{
		case EPCGExClusterElement::Vtx: PCGEX_RELAX_STEP_NODE(1)
			PCGEX_RELAX_STEP_NODE(2)
			PCGEX_RELAX_STEP_NODE(3)
			break;
		case EPCGExClusterElement::Edge: PCGEX_RELAX_STEP_EDGE(1)
			PCGEX_RELAX_STEP_EDGE(2)
			PCGEX_RELAX_STEP_EDGE(3)
			break;
		}

#undef PCGEX_RELAX_PROGRESS
#undef PCGEX_RELAX_STEP_NODE
#undef PCGEX_RELAX_STEP_EDGE
	}

	void FProcessor::PrepareLoopScopesForNodes(const TArray<PCGExMT::FScope>& Loops)
	{
		TProcessor<FPCGExRelaxClustersContext, UPCGExRelaxClustersSettings>::PrepareLoopScopesForNodes(Loops);
		MaxDistanceValue = MakeShared<PCGExMT::TScopedNumericValue<double>>(Loops, 0);
	}

	void FProcessor::ProcessNodes(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExClusters::FNode>& Nodes = *Cluster->Nodes;

		TPCGValueRange<FTransform> OutTransforms = VtxDataFacade->GetOut()->GetTransformValueRange(false);

		TArray<FTransform>& WBufferRef = (*RelaxOperation->WriteBuffer);

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExClusters::FNode& Node = Nodes[Index];

			if (!InfluenceDetails.bProgressiveInfluence)
			{
				OutTransforms[Node.PointIndex] = PCGExTypeOps::FTypeOps<FTransform>::Lerp(OutTransforms[Node.PointIndex], WBufferRef[Node.Index], InfluenceDetails.GetInfluence(Node.PointIndex));
			}
			else
			{
				OutTransforms[Node.PointIndex] = WBufferRef[Node.Index];
			}

			const FVector DirectionAndSize = OutTransforms[Node.PointIndex].GetLocation() - Cluster->GetPos(Node.Index);

			PCGEX_OUTPUT_VALUE(DirectionAndSize, Node.PointIndex, DirectionAndSize)
			PCGEX_OUTPUT_VALUE(Direction, Node.PointIndex, DirectionAndSize.GetSafeNormal())
			PCGEX_OUTPUT_VALUE(Amplitude, Node.PointIndex, DirectionAndSize.Length())
		}
	}

	void FProcessor::OnNodesProcessingComplete()
	{
		TProcessor<FPCGExRelaxClustersContext, UPCGExRelaxClustersSettings>::OnNodesProcessingComplete();
		Cluster->WillModifyVtxPositions(true);
		ForwardCluster();
	}

	FBatch::FBatch(FPCGExContext* InContext, const TSharedRef<PCGExData::FPointIO>& InVtx, TArrayView<TSharedRef<PCGExData::FPointIO>> InEdges)
		: TBatch(InContext, InVtx, InEdges)
	{
	}

	FBatch::~FBatch()
	{
	}

	void FBatch::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TBatch<FProcessor>::RegisterBuffersDependencies(FacadePreloader);
		GetContext<FPCGExRelaxClustersContext>()->Relaxing->RegisterPrimaryBuffersDependencies(ExecutionContext, FacadePreloader);

		PCGEX_TYPED_CONTEXT_AND_SETTINGS(RelaxClusters)

		{
			const TSharedRef<PCGExData::FFacade>& OutputFacade = VtxDataFacade;
			PCGEX_FOREACH_FIELD_RELAX_CLUSTER(PCGEX_OUTPUT_INIT)
		}
	}

	bool FBatch::PrepareSingle(const TSharedPtr<PCGExClusterMT::IProcessor>& InProcessor)
	{
		if (!TBatch<FProcessor>::PrepareSingle(InProcessor)) { return false; }

		PCGEX_TYPED_PROCESSOR

#define PCGEX_OUTPUT_FWD_TO(_NAME, _TYPE, _DEFAULT_VALUE) if(_NAME##Writer){ TypedProcessor->_NAME##Writer = _NAME##Writer; }
		PCGEX_FOREACH_FIELD_RELAX_CLUSTER(PCGEX_OUTPUT_FWD_TO)
#undef PCGEX_OUTPUT_FWD_TO

		return true;
	}

	void FBatch::Write()
	{
		TBatch<FProcessor>::Write();
		VtxDataFacade->WriteFastest(TaskManager);
	}
}


#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
