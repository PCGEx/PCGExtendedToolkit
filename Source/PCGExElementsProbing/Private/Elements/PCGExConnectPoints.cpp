// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExConnectPoints.h"

#include "Core/PCGExFilterTypeSets.h"
#include "Core/PCGExPointFilter.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "Core/PCGExProbingEngine.h"
#include "Data/PCGExClusterData.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"
#include "Graphs/PCGExGraph.h"
#include "Graphs/PCGExGraphBuilder.h"

#define LOCTEXT_NAMESPACE "PCGExConnectPointsElement"
#define PCGEX_NAMESPACE BuildCustomGraph

TArray<FPCGPinProperties> UPCGExConnectPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExClusters::Labels::SourceProbesLabel, "Probes used to connect points", Required, FPCGExDataTypeInfoProbe::AsId())

	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceFilterGenerators, "Points that don't meet requirements won't generate connections", Normal)
	PCGEX_PIN_FILTERS(PCGExClusters::Labels::SourceFilterConnectables, "Points that don't meet requirements can't receive connections", Normal)

	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExConnectPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	PCGEX_PIN_POINTS(PCGExClusters::Labels::OutputEdgesLabel, "Point data representing edges.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(ConnectPoints)
PCGEX_ELEMENT_BATCH_POINT_IMPL(ConnectPoints)

bool FPCGExConnectPointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(ConnectPoints)

	if (!PCGExFactories::GetInputFactories<UPCGExProbeFactoryData>(Context, PCGExClusters::Labels::SourceProbesLabel, Context->ProbeFactories, {FPCGExDataTypeInfoProbe::AsId()}))
	{
		return false;
	}

	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceFilterGenerators, Context->GeneratorsFiltersFactories, PCGExFactories::PointFilters(), false);
	PCGExFactories::GetInputFactories(Context, PCGExClusters::Labels::SourceFilterConnectables, Context->ConnectablesFiltersFactories, PCGExFactories::PointFilters(), false);

	Context->CWCoincidenceTolerance = FVector(Settings->CoincidenceTolerance);

	return true;
}

bool FPCGExConnectPointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExConnectPointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(ConnectPoints)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some input have less than 2 points and will be ignored."))
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Entry->GetNum() < 2)
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters. Make sure inputs have at least 2 points."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();
	Context->MainBatch->Output();

	return Context->TryComplete();
}

namespace PCGExConnectPoints
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExConnectPoints::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		const int32 NumPoints = PointDataFacade->GetNum();

		Engine = MakeShared<PCGExProbing::FProbingEngine>(PointDataFacade);
		Engine->SetCoincidence(Settings->bPreventCoincidence, Context->CWCoincidenceTolerance);
		if (Settings->bProjectPoints)
		{
			Engine->SetProjection(Settings->ProjectionDetails);
		}

		if (!Engine->Init(Context, Context->ProbeFactories))
		{
			return false;
		}

		if (!PointDataFacade->Source->InitializeOutput<UPCGExClusterNodesData>(PCGExData::EIOInit::New))
		{
			return false;
		}
		GraphBuilder = MakeShared<PCGExGraphs::FGraphBuilder>(PointDataFacade, &Settings->GraphBuilderDetails);

		if (!Context->GeneratorsFiltersFactories.IsEmpty())
		{
			GeneratorsFilter = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
			if (!GeneratorsFilter->Init(ExecutionContext, Context->GeneratorsFiltersFactories))
			{
				return false;
			}
		}

		if (!Context->ConnectablesFiltersFactories.IsEmpty())
		{
			ConnectableFilter = MakeShared<PCGExPointFilter::FManager>(PointDataFacade);
			if (!ConnectableFilter->Init(ExecutionContext, Context->ConnectablesFiltersFactories))
			{
				return false;
			}
		}

		PCGEX_ASYNC_GROUP_CHKD(TaskManager, PrepTask)

		PrepTask->OnCompleteCallback = [PCGEX_ASYNC_THIS_CAPTURE]()
		{
			PCGEX_ASYNC_THIS
			This->OnPreparationComplete();
		};

		PrepTask->OnSubLoopStartCallback = [PCGEX_ASYNC_THIS_CAPTURE](const PCGExMT::FScope& Scope)
		{
			PCGEX_ASYNC_THIS
			This->PointDataFacade->Fetch(Scope);

			PCGEX_SCOPE_LOOP(i)
			{
				This->Engine->CanGenerate[i] = This->GeneratorsFilter ? This->GeneratorsFilter->Test(i) : true;
				This->Engine->AcceptConnections[i] = This->ConnectableFilter ? This->ConnectableFilter->Test(i) : true;
			}
		};

		PrepTask->StartSubLoops(NumPoints, PCGEX_CORE_SETTINGS.GetPointsBatchChunkSize());

		return true;
	}

	void FProcessor::OnPreparationComplete()
	{
		GeneratorsFilter.Reset();
		ConnectableFilter.Reset();

		Engine->RunAsync(
			TaskManager,
			[PCGEX_ASYNC_THIS_CAPTURE]()
			{
				PCGEX_ASYNC_THIS
				This->GraphBuilder->Graph->InsertEdges_Unsafe(This->Engine->GetUniqueEdges(), -1);
				This->GraphBuilder->CompileAsync(This->TaskManager, true);
			});
	}

	void FProcessor::CompleteWork()
	{
		if (!GraphBuilder->bCompiledSuccessfully)
		{
			PCGEX_CLEAR_IO_VOID(PointDataFacade->Source)
		}
	}

	void FProcessor::Output()
	{
		GraphBuilder->StageEdgesOutputs();
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExConnectPointsContext, UPCGExConnectPointsSettings>::Cleanup();
		Engine.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
