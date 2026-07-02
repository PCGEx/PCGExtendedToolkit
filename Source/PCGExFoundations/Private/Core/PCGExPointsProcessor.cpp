// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPointsProcessor.h"

#include "PCGPin.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExPointIO.h"
#include "Math/PCGExProjectionDetails.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

#if WITH_EDITOR
void UPCGExPointsProcessorSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 73, 4)
	{
		for (TFieldIterator<FProperty> PropIt(GetClass()); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;

			// Update Geo 2D projection
			if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
			{
				if (StructProp->Struct == FPCGExGeo2DProjectionDetails::StaticStruct())
				{
					FPCGExGeo2DProjectionDetails* ProjectionDetails = StructProp->ContainerPtrToValuePtr<FPCGExGeo2DProjectionDetails>(this);

					if (ProjectionDetails)
					{
						ProjectionDetails->ApplyDeprecation();
						break; // There should be only one
					}
				}
			}
		}
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

TArray<FPCGPinProperties> UPCGExPointsProcessorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (!IsInputless())
	{
		if (!GetIsMainTransactional())
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
		else
		{
			if (GetMainAcceptMultipleData())
			{
				PCGEX_PIN_ANY(GetMainInputPin(), "The data to be processed.", Required)
			}
			else
			{
				PCGEX_PIN_ANY(GetMainInputPin(), "The data to be processed.", Required)
			}
		}
	}

	InputPinPropertiesBeforeFilters(PinProperties);

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

	return PinProperties;
}

void UPCGExPointsProcessorSettings::InputPinPropertiesBeforeFilters(TArray<FPCGPinProperties>& PinProperties) const
{
}


TArray<FPCGPinProperties> UPCGExPointsProcessorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(GetMainOutputPin(), "The processed input.", Normal)
	return PinProperties;
}

PCGExData::EIOInit UPCGExPointsProcessorSettings::GetMainOutputInitMode() const
{
	return PCGExData::EIOInit::NoInit;
}

TSet<FPCGDataTypeBaseId> UPCGExPointsProcessorSettings::GetPointFilterTypes() const
{
	return PCGExFactories::PointFilters();
}

FPCGExPointsProcessorContext::~FPCGExPointsProcessorContext()
{
	if (MainBatch)
	{
		MainBatch->Cleanup();
	}
	MainBatch.Reset();
}

bool FPCGExPointsProcessorContext::AdvancePointsIO(const bool bCleanupKeys)
{
	if (bCleanupKeys && CurrentIO)
	{
		CurrentIO->ClearCachedKeys();
	}

	if (MainPoints->Pairs.IsValidIndex(++CurrentPointIOIndex))
	{
		CurrentIO = MainPoints->Pairs[CurrentPointIOIndex];
		return true;
	}

	CurrentIO = nullptr;
	return false;
}

#pragma endregion

bool FPCGExPointsProcessorContext::ProcessPointsBatch(const PCGExCommon::ContextState NextStateId)
{
	if (!bBatchProcessingEnabled)
	{
		return true;
	}

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExPointsMT::MTState_PointsProcessing)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPointsProcessorContext::ProcessPointsBatch::InitialProcessingDone);
		BatchProcessing_InitialProcessingDone();

		SetState(PCGExPointsMT::MTState_PointsCompletingWork);
		if (!MainBatch->bSkipCompletion)
		{
			{
				PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), false)
				MainBatch->CompleteWork();
			}
			if (IsWaitingForTasks())
			{
				return false;
			}
		}
	}

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExPointsMT::MTState_PointsCompletingWork)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPointsProcessorContext::ProcessPointsBatch::WorkComplete);
		if (!MainBatch->bSkipCompletion)
		{
			BatchProcessing_WorkComplete();
		}

		if (MainBatch->bRequiresWriteStep)
		{
			SetState(PCGExPointsMT::MTState_PointsWriting);
			{
				PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), false)
				MainBatch->Write();
			}
			if (IsWaitingForTasks())
			{
				return false;
			}
		}
		
		bBatchProcessingEnabled = false;
		
		if (NextStateId == PCGExCommon::States::State_Done)
		{
			Done();
		}
		
		SetState(NextStateId);
		return true;
	}

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExPointsMT::MTState_PointsWriting)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPointsProcessorContext::ProcessPointsBatch::WritingDone);
		BatchProcessing_WritingDone();

		bBatchProcessingEnabled = false;
		if (NextStateId == PCGExCommon::States::State_Done)
		{
			Done();
		}
		
		SetState(NextStateId);
	}

	return !IsWaitingForTasks();
}

bool FPCGExPointsProcessorContext::StartBatchProcessingPoints(FBatchProcessingValidateEntry&& ValidateEntry, FBatchProcessingInitPointBatch&& InitBatch)
{
	bBatchProcessingEnabled = false;

	MainBatch.Reset();

	PCGEX_SETTINGS_LOCAL(PointsProcessor)

	SubProcessorMap.Empty();
	SubProcessorMap.Reserve(MainPoints->Num());

	TArray<TWeakPtr<PCGExData::FPointIO>> BatchAblePoints;
	BatchAblePoints.Reserve(InitialMainPointsNum);

	while (AdvancePointsIO(false))
	{
		if (!ValidateEntry(CurrentIO))
		{
			continue;
		}
		BatchAblePoints.Add(CurrentIO.ToSharedRef());
	}

	if (BatchAblePoints.IsEmpty())
	{
		return bBatchProcessingEnabled;
	}
	bBatchProcessingEnabled = true;

	const TSharedPtr<PCGExPointsMT::IBatch> NewBatch = CreatePointBatchInstance(BatchAblePoints);
	MainBatch = NewBatch;
	MainBatch->SubProcessorMap = &SubProcessorMap;
	MainBatch->DataInitializationPolicy = Settings->WantsBulkInitData() ? Settings->GetMainDataInitializationPolicy() : PCGExData::EIOInit::NoInit;

	InitBatch(NewBatch);

	if (Settings->SupportsPointFilters())
	{
		NewBatch->SetPointsFilterData(&FilterFactories);
	}

	if (MainBatch->PrepareProcessing())
	{
		SetState(PCGExPointsMT::MTState_PointsProcessing);
		PCGEX_ASYNC_SCHEDULING_SCOPE(GetTaskManager(), bBatchProcessingEnabled)
		MainBatch->Process(GetTaskManager());
	}
	else
	{
		bBatchProcessingEnabled = false;
	}

	return bBatchProcessingEnabled;
}

void FPCGExPointsProcessorContext::BatchProcessing_InitialProcessingDone()
{
}

void FPCGExPointsProcessorContext::BatchProcessing_WorkComplete()
{
}

void FPCGExPointsProcessorContext::BatchProcessing_WritingDone()
{
}

void FPCGExPointsProcessorElement::DisabledPassThroughData(FPCGContext* Context) const
{
	const UPCGExPointsProcessorSettings* Settings = Context->GetInputSettings<UPCGExPointsProcessorSettings>();
	check(Settings);

	//Forward main points
	TArray<FPCGTaggedData> MainSources = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());
	for (const FPCGTaggedData& TaggedData : MainSources)
	{
		FPCGTaggedData& TaggedDataCopy = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedDataCopy.Data = TaggedData.Data;
		TaggedDataCopy.Tags.Append(TaggedData.Tags);
		TaggedDataCopy.Pin = Settings->GetMainOutputPin();
	}
}

bool FPCGExPointsProcessorElement::Boot(FPCGExContext* InContext) const
{
	if (!IPCGExElement::Boot(InContext))
	{
		return false;
	}

	FPCGExPointsProcessorContext* Context = static_cast<FPCGExPointsProcessorContext*>(InContext);
	PCGEX_SETTINGS(PointsProcessor)

	if (Context->InputData.GetAllInputs().IsEmpty() && !Settings->IsInputless())
	{
		return false;
	} //Get rid of errors and warning when there is no input

	Context->MainPoints = MakeShared<PCGExData::FPointIOCollection>(Context, Settings->GetIsMainTransactional());
	Context->MainPoints->OutputPin = Settings->GetMainOutputPin();

	TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());
	if (Sources.IsEmpty() && !Settings->IsInputless())
	{
		return false;
	} // Silent cancel, there's simply no data

	if (Settings->GetMainAcceptMultipleData())
	{
		Context->MainPoints->Initialize(Sources);
	}
	else
	{
		if (const TSharedPtr<PCGExData::FPointIO> SingleInput = PCGExData::TryGetSingleInput(Context, Settings->GetMainInputPin(), false, false))
		{
			Context->MainPoints->Add_Unsafe(SingleInput);
		}
	}

	Context->InitialMainPointsNum = Context->MainPoints->Num();

	if (Context->MainPoints->IsEmpty() && !Settings->IsInputless())
	{
		PCGEX_LOG_MISSING_INPUT(Context, FText::Format(FText::FromString(TEXT("Missing {0} inputs (either no data or no points)")), FText::FromName(Settings->GetMainInputPin())))
		return false;
	}

	if (Settings->SupportsPointFilters())
	{
		if (const bool bRequiredFilters = Settings->RequiresPointFilters();
			!PCGExFactories::GetInputFactories(Context, Settings->GetPointFilterPin(), Context->FilterFactories, Settings->GetPointFilterTypes(), bRequiredFilters) && bRequiredFilters)
		{
			return false;
		}
	}

	return true;
}

void FPCGExPointsProcessorElement::InitializeData(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	IPCGExElement::InitializeData(InContext, InSettings);

	FPCGExPointsProcessorContext* Context = static_cast<FPCGExPointsProcessorContext*>(InContext);
	PCGEX_SETTINGS(PointsProcessor)

	PCGExData::EIOInit InitMode = Settings->GetMainOutputInitMode();
	if (InitMode != PCGExData::EIOInit::NoInit)
	{
		for (const TSharedPtr<PCGExData::FPointIO>& IO : Context->MainPoints->Pairs)
		{
			IO->InitializeOutput(InitMode);
		}
	}
}

#undef LOCTEXT_NAMESPACE
