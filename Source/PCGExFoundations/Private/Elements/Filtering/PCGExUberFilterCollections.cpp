// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/Filtering/PCGExUberFilterCollections.h"


#include "PCGExPickersCommon.h"
#include "Core/PCGExPickerFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExUberFilterCollections"
#define PCGEX_NAMESPACE UberFilterCollections

bool UPCGExUberFilterCollectionsSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExPickers::Labels::SourcePickersLabel)
	{
		return InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

bool UPCGExUberFilterCollectionsSettings::HasDynamicPins() const
{
	return true;
}

TArray<FPCGPinProperties> UPCGExUberFilterCollectionsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExPickers::Labels::SourcePickersLabel, "A precise selection of point that will be tested, as opposed to all of them.", Normal, FPCGExDataTypeInfoPicker::AsId())
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExUberFilterCollectionsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(PCGExFilters::Labels::OutputInsideFiltersLabel, "Collections that passed the filters.", Required)
	PCGEX_PIN_ANY(PCGExFilters::Labels::OutputOutsideFiltersLabel, "Collections that didn't pass the filters.", Required)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(UberFilterCollections)
PCGEX_ELEMENT_BATCH_POINT_IMPL(UberFilterCollections)

FName UPCGExUberFilterCollectionsSettings::GetMainOutputPin() const
{
	// Ensure proper forward when node is disabled
	return PCGExFilters::Labels::OutputInsideFiltersLabel;
}

bool UPCGExUberFilterCollectionsSettings::GetIsMainTransactional() const
{
	return true;
}

bool FPCGExUberFilterCollectionsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCollections)

	PCGExFactories::GetInputFactories(Context, PCGExPickers::Labels::SourcePickersLabel, Context->PickerFactories, {FPCGExDataTypeInfoPicker::AsId()}, false);

	Context->Inside = MakeShared<PCGExData::FPointIOCollection>(Context, true);
	Context->Outside = MakeShared<PCGExData::FPointIOCollection>(Context, true);

	Context->Inside->OutputPin = PCGExFilters::Labels::OutputInsideFiltersLabel;
	Context->Outside->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;

	if (Settings->bSwap)
	{
		Context->Inside->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;
		Context->Outside->OutputPin = PCGExFilters::Labels::OutputInsideFiltersLabel;
	}

	Context->bHasOnlyCollectionFilters = true;
	for (const TObjectPtr<const UPCGExPointFilterFactoryData>& FilterFactory : Context->FilterFactories)
	{
		if (!FilterFactory->SupportsCollectionEvaluation())
		{
			Context->bHasOnlyCollectionFilters = false;
			break;
		}
	}

	return true;
}

bool FPCGExUberFilterCollectionsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterCollectionsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(UberFilterCollections)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->bHasOnlyCollectionFilters)
		{
			Context->NumPairs = Context->MainPoints->Pairs.Num();

			if (!Context->StartBatchProcessingPoints(
				[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
				{
					return true;
				},
				[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
				{
					NewBatch->bSkipCompletion = Context->bHasOnlyCollectionFilters;
				}))
			{
				return Context->CancelExecution(TEXT("Could not find any points to filter."));
			}
		}
		else
		{
			PCGEX_MAKE_SHARED(DummyFacade, PCGExData::FFacade, Context->MainPoints->Pairs[0].ToSharedRef())
			PCGEX_MAKE_SHARED(PrimaryFilters, PCGExPointFilter::FManager, DummyFacade.ToSharedRef())
			PrimaryFilters->bWillBeUsedWithCollections = true;
			PrimaryFilters->Init(Context, Context->FilterFactories);

			while (Context->AdvancePointsIO())
			{
				if (PrimaryFilters->Test(Context->CurrentIO, Context->MainPoints))
				{
					Context->Inside->Add_Unsafe(Context->CurrentIO);
				}
				else
				{
					Context->Outside->Add_Unsafe(Context->CurrentIO);
				}
			}

			Context->Done();
		}
	}

	if (!Context->bHasOnlyCollectionFilters)
	{
		PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)
		Context->MainBatch->Output();
	}

	uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;
	if (!Context->Inside->StageAnyOutputs())
	{
		Mask |= 1ULL << (Settings->bSwap ? 1 : 0);
	}
	if (!Context->Outside->StageAnyOutputs())
	{
		Mask |= 1ULL << (Settings->bSwap ? 0 : 1);
	}

	return Context->TryComplete();
}

namespace PCGExUberFilterCollections
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExUberFilterCollections::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PointDataFacade->Source->bAllowEmptyOutput = true;

		bUsePicks = PCGExPickers::GetPicks(Context->PickerFactories, PointDataFacade, Picks);
		NumPoints = bUsePicks ? Picks.Num() : PointDataFacade->GetNum();

		if (Settings->Measure == EPCGExMeanMeasure::Discrete)
		{
			if ((Settings->Comparison == EPCGExComparison::StrictlyGreater || Settings->Comparison == EPCGExComparison::EqualOrGreater) && NumPoints < Settings->IntThreshold)
			{
				// Not enough points to meet requirements.
				Context->Outside->Add(PointDataFacade->Source);
				return true;
			}
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::UberFilterCollections::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		if (bUsePicks)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!Picks.Contains(Index))
				{
					continue;
				}
				if (PointFilterCache[Index])
				{
					FPlatformAtomics::InterlockedAdd(&NumInside, 1);
				}
				else
				{
					FPlatformAtomics::InterlockedAdd(&NumOutside, 1);
				}
			}
		}
		else
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				if (PointFilterCache[Index])
				{
					FPlatformAtomics::InterlockedAdd(&NumInside, 1);
				}
				else
				{
					FPlatformAtomics::InterlockedAdd(&NumOutside, 1);
				}
			}
		}
	}

	void FProcessor::Output()
	{
		IProcessor::Output();

		TSharedPtr<PCGExData::FPointIO> OutIO;
		
		switch (Settings->Mode)
		{
		default: case EPCGExUberFilterCollectionsMode::All:
			if (NumInside == NumPoints)
			{
				OutIO = Context->Inside->Add(PointDataFacade->Source);
			}
			else
			{
				OutIO = Context->Outside->Add(PointDataFacade->Source);
			}
			break;
		case EPCGExUberFilterCollectionsMode::Any:
			if (NumInside != 0)
			{
				OutIO = Context->Inside->Add(PointDataFacade->Source);
			}
			else
			{
				OutIO = Context->Outside->Add(PointDataFacade->Source);
			}
			break;
		case EPCGExUberFilterCollectionsMode::Partial:
			if (Settings->Measure == EPCGExMeanMeasure::Discrete)
			{
				if (PCGExCompare::Compare(Settings->Comparison, NumInside, Settings->IntThreshold, 0))
				{
					OutIO = Context->Inside->Add(PointDataFacade->Source);
				}
				else
				{
					OutIO = Context->Outside->Add(PointDataFacade->Source);
				}
			}
			else
			{
				const double Ratio = static_cast<double>(NumInside) / static_cast<double>(NumPoints);
				if (PCGExCompare::Compare(Settings->Comparison, Ratio, Settings->DblThreshold, Settings->Tolerance))
				{
					OutIO = Context->Inside->Add(PointDataFacade->Source);
				}
				else
				{
					OutIO = Context->Outside->Add(PointDataFacade->Source);
				}
			}
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
