// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Bitmasks/PCGExBitwiseOperation.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Details/PCGExSettingsDetails.h"

#define LOCTEXT_NAMESPACE "PCGExBitwiseOperationElement"
#define PCGEX_NAMESPACE BitwiseOperation

#if WITH_EDITOR
void UPCGExBitwiseOperationSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		// Rewire Mask
		PCGEX_SHORTHAND_RENAME_PIN(MaskAttribute, Bitmask, Mask)
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExBitwiseOperationSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 7)
	{
		Mask.Update(MaskInput_DEPRECATED, MaskAttribute_DEPRECATED, Bitmask_DEPRECATED);
	}
	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

PCGEX_INITIALIZE_ELEMENT(BitwiseOperation)

PCGExData::EIOInit UPCGExBitwiseOperationSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(BitwiseOperation)

bool FPCGExBitwiseOperationElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(BitwiseOperation)

	PCGEX_VALIDATE_NAME(Settings->FlagAttribute)

	return true;
}

bool FPCGExBitwiseOperationElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExBitwiseOperationElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(BitwiseOperation)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		// Selector validity (incl. properties and @Data) is handled by Mask->Init in the processor.
		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
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

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		Mask = Settings->Mask.GetValueSetting();
		if (!Mask->Init(PointDataFacade))
		{
			return false;
		}

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
