// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/PCGExBatchActions.h"

#include "PCGExActionsCommon.h"
#include "Core/PCGExActionFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Types/PCGExAttributeIdentity.h"


#define LOCTEXT_NAMESPACE "PCGExGraphs"
#define PCGEX_NAMESPACE BatchActions

PCGExData::EIOInit UPCGExBatchActionsSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::Forward;
}

TArray<FPCGPinProperties> UPCGExBatchActionsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExActions::Labels::SourceActionsLabel, "Actions nodes.", Normal, FPCGExDataTypeInfoAction::AsId())
	PCGEX_PIN_ANY(PCGExActions::Labels::SourceDefaultsLabel, "Default values that match attributes when creating new attributes.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(BatchActions)

PCGExData::EIOInit UPCGExBatchActionsSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(BatchActions)

bool FPCGExBatchActionsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	// Grab all param set attributes

	PCGEX_CONTEXT_AND_SETTINGS(BatchActions)

	if (!PCGExFactories::GetInputFactories(Context, PCGExActions::Labels::SourceActionsLabel, Context->ActionsFactories, {FPCGExDataTypeInfoAction::AsId()}))
	{
		// No action factories, early exit.
		Context->ActionsFactories.Empty();
		return true;
	}

	FPCGExAttributeGatherDetails DefaultAttributesFilter = Settings->DefaultAttributesFilter;
	DefaultAttributesFilter.Init();
	Context->DefaultAttributes = PCGExData::GatherAttributeInfos(InContext, PCGExActions::Labels::SourceDefaultsLabel, DefaultAttributesFilter, true);

	if (!Context->DefaultAttributes)
	{
		return false;
	}

	FString Message = TEXT("An unspecified error occured.");
	bool bIsBatchActionsValid = true;
	PCGEX_MAKE_SHARED(ValidationInfos, PCGExData::FAttributesInfos)
	for (const TObjectPtr<const UPCGExActionFactoryData>& Factory : Context->ActionsFactories)
	{
		if (!Factory->AppendAndValidate(ValidationInfos, Message))
		{
			PCGE_LOG(Error, GraphAndLog, FText::FromString(Message));
			bIsBatchActionsValid = false;
			break;
		}
	}

	if (!bIsBatchActionsValid)
	{
		return false;
	}

	// TODO : Also check against provided default values

	return true;
}

bool FPCGExBatchActionsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBatchActionsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BatchActions)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->ActionsFactories.IsEmpty())
		{
			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					return true;
				},
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
				}))
			{
				return Context->CancelExecution(TEXT("Could not find any points to process."));
			}
		}
		else
		{
			// Early exit forward if no action should be processed
			Context->Done();
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExBatchActions
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBatchActions::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;
		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		// Initialize writers with provided default value
		for (const PCGExData::FAttributeIdentity& Identity : Context->DefaultAttributes->Identities)
		{
			const FPCGMetadataAttributeBase* AttributeBase = Identity.Attribute;
			if (!AttributeBase)
			{
				continue;
			}
			PCGExMetaHelpers::ExecuteWithRightType(
				AttributeBase,
				[&](auto DummyValue)
				{
					using T = decltype(DummyValue);
					PointDataFacade->GetWritable<T>(AttributeBase, PCGExData::EBufferInit::Inherit);
				},
				[&]()
				{
					// Property-backed: route through the generic GetWritableFromAttribute fallback.
					PointDataFacade->GetWritableFromAttribute(AttributeBase, PCGExData::EBufferInit::Inherit);
				});
		}

		for (const UPCGExActionFactoryData* Factory : Context->ActionsFactories)
		{
			TSharedPtr<FPCGExActionOperation> Operation = Factory->CreateOperation(Context);
			if (!Operation->PrepareForData(ExecutionContext, PointDataFacade))
			{
				return false;
			}
			Operations.Add(Operation);
		}

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BatchActions::ProcessPoints);

		PointDataFacade->Fetch(Scope);

		PCGEX_SCOPE_LOOP(Index)
		{
			for (const TSharedPtr<FPCGExActionOperation>& Op : Operations)
			{
				Op->ProcessPoint(Index);
			}
		}
	}

	void FProcessor::CompleteWork()
	{
		if (Settings->bDoConsumeProcessedAttributes)
		{
			for (const TSharedPtr<PCGExData::IBuffer>& DataCache : PointDataFacade->Buffers)
			{
				if (!DataCache->InAttribute || !Settings->ConsumeProcessedAttributes.Test(DataCache->InAttribute) || PCGExMetaHelpers::IsPCGExAttribute(DataCache->Identifier.Name))
				{
					continue;
				}

				PointDataFacade->Source->DeleteAttribute(DataCache->InAttribute->Name);
			}
		}

		PointDataFacade->WriteFastest(TaskManager);
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExBatchActionsContext, UPCGExBatchActionsSettings>::Cleanup();
		Operations.Empty();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
