// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bitmasks/PCGExBitwiseOperation.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExBitwiseOperationElement"
#define PCGEX_NAMESPACE BitwiseOperation

PCGEX_SETTING_VALUE_IMPL(UPCGExBitwiseOperationSettings, Mask, int64, MaskInput, MaskAttribute, Bitmask)

PCGEX_INITIALIZE_ELEMENT(BitwiseOperation)

PCGExData::EIOInit UPCGExBitwiseOperationSettings::GetMainDataInitializationPolicy() const { return PCGExData::EIOInit::Duplicate; }

PCGEX_ELEMENT_BATCH_POINT_IMPL(BitwiseOperation)

bool FPCGExBitwiseOperationElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(BitwiseOperation)

	PCGEX_VALIDATE_NAME(Settings->FlagAttribute)

	if (Settings->MaskInput == EPCGExInputValueType::Attribute)
	{
		PCGEX_VALIDATE_NAME_CONSUMABLE(Settings->MaskAttribute)
	}

	return true;
}

bool FPCGExBitwiseOperationElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBitwiseOperationElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BitwiseOperation)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		PCGEX_ON_INVALILD_INPUTS(FTEXT("Some inputs are missing the specified MaskAttribute and won't be processed."))

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				if (Settings->MaskInput == EPCGExInputValueType::Attribute && !Entry->GetIn()->Metadata->HasAttribute(Settings->MaskAttribute))
				{
					bHasInvalidInputs = true;
					return false;
				}
				return true;
			}, [&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to process."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExBitwiseOperation
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExBitwiseOperation::Process);

		if (!IProcessor::Process(InTaskManager)) { return false; }

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		Mask = Settings->GetValueSettingMask();
		if (!Mask->Init(PointDataFacade)) { return false; }

		Writer = PointDataFacade->GetWritable<int64>(Settings->FlagAttribute, 0, false, PCGExData::EBufferInit::Inherit);

		Op = Settings->Operation;

		StartParallelLoopForPoints();

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::BitwiseOperation::ProcessPoints);

		PCGEX_SCOPE_LOOP(Index)
		{
			int64 OutValue = Writer->GetValue(Index);
			PCGExBitmask::Do(Op, OutValue, Mask->Read(Index));
			Writer->SetValue(Index, OutValue);
		}
	}

	void FProcessor::CompleteWork()
	{
		PointDataFacade->WriteFastest(TaskManager);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
