// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Filtering/PCGExCherryPickPoints.h"


#include "Core/PCGExPickerFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactories.h"

#define LOCTEXT_NAMESPACE "PCGExCherryPickPointsElement"
#define PCGEX_NAMESPACE CherryPickPoints

PCGEX_INITIALIZE_ELEMENT(CherryPickPoints)
PCGEX_ELEMENT_BATCH_POINT_IMPL(CherryPickPoints)

TArray<FPCGPinProperties> UPCGExCherryPickPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExPickers::Labels::SourcePickersLabel, "Pickers config", Required, FPCGExDataTypeInfoPicker::AsId())
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExCherryPickPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::OutputPinProperties();
	if (bOutputDiscardedPoints)
	{
		PCGEX_PIN_POINTS(PCGExCommon::Labels::OutputDiscardedLabel, "Discarded points", Normal)
	}
	return PinProperties;
}

bool FPCGExCherryPickPointsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(CherryPickPoints)

	return PCGExFactories::GetInputFactories(Context, PCGExPickers::Labels::SourcePickersLabel, Context->PickerFactories, {FPCGExDataTypeInfoPicker::AsId()});
}

bool FPCGExCherryPickPointsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExCherryPickPointsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(CherryPickPoints)
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
			return Context->CancelExecution(TEXT("Could not find any data to cherry pick."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExCherryPickPoints
{
	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExCherryPickPoints::Process);

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PointDataFacade->Source->bAllowEmptyOutput = Settings->bAllowEmptyOutputs;

		TSet<int32> UniqueIndices;
		// Grab picks
		PCGExPickers::GetPicks(Context->PickerFactories, PointDataFacade, UniqueIndices);

		if (UniqueIndices.IsEmpty())
		{
			if (Settings->bOutputDiscardedPoints)
			{
				if (!Settings->bInvert)
				{
					PointDataFacade->Source->OutputPin = PCGExCommon::Labels::OutputDiscardedLabel;
				}
				PointDataFacade->Source->InitializeOutput(PCGExData::EIOInit::Forward);
			}
			else
			{
				if (Settings->bInvert)
				{
					PointDataFacade->Source->InitializeOutput(PCGExData::EIOInit::Forward);
				}
				else
				{
					PointDataFacade->Source->Disable();
				}
			}
			return true;
		}

		if (!PointDataFacade->Source->InitializeOutput(PCGExData::EIOInit::New))
		{
			return false;
		}

		const UPCGBasePointData* SourcePoints = PointDataFacade->GetIn();
		const int32 NumPoints = SourcePoints->GetNumPoints();
		const int32 NumPicks = FMath::Min(UniqueIndices.Num(), NumPoints);

		TArray<int32> PickedIndices;
		TArray<int32> DiscardedIndices;

		PickedIndices.Reserve(NumPicks);
		DiscardedIndices.Reserve(NumPoints - NumPicks);

		if (Settings->bInvert)
		{
			for (int i = 0; i < NumPoints; i++)
			{
				if (!UniqueIndices.Contains(i))
				{
					PickedIndices.Add(i);
				}
				else
				{
					DiscardedIndices.Add(i);
				}
			}
		}
		else
		{
			for (int i = 0; i < NumPoints; i++)
			{
				if (UniqueIndices.Contains(i))
				{
					PickedIndices.Add(i);
				}
				else
				{
					DiscardedIndices.Add(i);
				}
			}
		}

		PointDataFacade->Source->InheritPoints(PickedIndices, 0);

		if (Settings->bOutputDiscardedPoints)
		{
			if (TSharedPtr<PCGExData::FPointIO> Discarded = Context->MainPoints->Emplace_GetRef(PointDataFacade->Source, PCGExData::EIOInit::New))
			{
				PointDataFacade->Source->InheritPoints(DiscardedIndices, 0);
			}
		}

		return true;
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
