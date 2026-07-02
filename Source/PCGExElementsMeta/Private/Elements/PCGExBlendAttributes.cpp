// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBlendAttributes.h"

#include "Core/PCGExBlendOpsManager.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExBlendingDetails.h"


#define LOCTEXT_NAMESPACE "PCGExBlendAttributesElement"
#define PCGEX_NAMESPACE BlendAttributes

TArray<FPCGPinProperties> UPCGExBlendAttributesSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGExBlending::DeclareBlendOpsInputs(PinProperties, EPCGPinStatus::Required);
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BlendAttributes)

PCGExData::EIOInit UPCGExBlendAttributesSettings::GetMainDataInitializationPolicy() const
{
	return WantsDataStealing() ? PCGExData::EIOInit::Forward : PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(BlendAttributes)

bool FPCGExBlendAttributesElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BlendAttributes)

	return PCGExFactories::GetInputFactories<UPCGExBlendOpFactory>(Context, PCGExBlending::Labels::SourceBlendingLabel, Context->BlendingFactories, {FPCGExDataTypeInfoBlendOp::AsId()});
}

bool FPCGExBlendAttributesElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBlendAttributesElement::Execute);

	PCGEX_CONTEXT(BlendAttributes)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bSkipCompletion = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExBlendAttributes
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBlendAttributes::Process);

		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		BlendOpsManager = MakeShared<PCGExBlending::FBlendOpsManager>();
		BlendOpsManager->SetSources(PointDataFacade, PCGExData::EIOSide::Out);
		BlendOpsManager->SetTargetFacade(PointDataFacade);

		if (!BlendOpsManager->Init(Context, Context->BlendingFactories))
		{
			return false;
		}

		StartParallelLoopForPoints();
		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		BlendOpsManager->BlendAutoWeight(Scope, Scope.GetConstView(PointFilterCache));
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		BlendOpsManager->Cleanup(Context);
		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExBlendAttributesContext, UPCGExBlendAttributesSettings>::Cleanup();
		BlendOpsManager.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
