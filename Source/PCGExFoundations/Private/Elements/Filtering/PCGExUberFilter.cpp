// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/Filtering/PCGExUberFilter.h"

#include "PCGExPickersCommon.h"
#include "PCGExVersion.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExPickerFactoryProvider.h"
#include "Core/PCGExPointFilter.h"
#include "Data/PCGExData.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExArrayHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExUberFilter"
#define PCGEX_NAMESPACE UberFilter

#if WITH_EDITOR
void UPCGExUberFilterSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 70, 11)
	{
		if (!ResultAttributeName_DEPRECATED.IsNone())
		{
			ResultDetails.ResultAttributeName = ResultAttributeName_DEPRECATED;
		}
	}

	PCGEX_IF_VERSION_LOWER(1, 71, 2)
	{
		ResultDetails.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}
#endif

bool UPCGExUberFilterSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (InPin->Properties.Label == PCGExPickers::Labels::SourcePickersLabel)
	{
		return InPin->EdgeCount() > 0;
	}
	return Super::IsPinUsedByNodeExecution(InPin);
}

bool UPCGExUberFilterSettings::OutputPinsCanBeDeactivated() const
{
	return Mode != EPCGExUberFilterMode::Write;
}

TArray<FPCGPinProperties> UPCGExUberFilterSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExPickers::Labels::SourcePickersLabel, "A precise selection of point that will be tested, as opposed to all of them.", Normal, FPCGExDataTypeInfoPicker::AsId())
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExUberFilterSettings::OutputPinProperties() const
{
	if (Mode == EPCGExUberFilterMode::Write)
	{
		return Super::OutputPinProperties();
	}

	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExFilters::Labels::OutputInsideFiltersLabel, "Points that passed the filters.", Required)
	if (bOutputDiscardedElements)
	{
		PCGEX_PIN_POINTS(PCGExFilters::Labels::OutputOutsideFiltersLabel, "Points that didn't pass the filters.", Required)
	}
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(UberFilter)
PCGEX_ELEMENT_BATCH_POINT_IMPL(UberFilter)

PCGExData::EIOInit UPCGExUberFilterSettings::GetMainDataInitializationPolicy() const
{
	return
		Mode == EPCGExUberFilterMode::Write
		? WantsDataStealing()
		? PCGExData::EIOInit::Forward
		: PCGExData::EIOInit::Duplicate
		: PCGExData::EIOInit::NoInit;
}

FName UPCGExUberFilterSettings::GetMainOutputPin() const
{
	// Ensure proper forward when node is disabled
	return Mode == EPCGExUberFilterMode::Partition ? PCGExFilters::Labels::OutputInsideFiltersLabel : Super::GetMainOutputPin();
}

bool FPCGExUberFilterElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(UberFilter)

	PCGExFactories::GetInputFactories(Context, PCGExPickers::Labels::SourcePickersLabel, Context->PickerFactories, {FPCGExDataTypeInfoPicker::AsId()}, false);

	if (Settings->Mode == EPCGExUberFilterMode::Write)
	{
		return Settings->ResultDetails.Validate(Context);
	}

	Context->Inside = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->Outside = MakeShared<PCGExData::FPointIOCollection>(Context);

	Context->Inside->OutputPin = PCGExFilters::Labels::OutputInsideFiltersLabel;
	Context->Outside->OutputPin = PCGExFilters::Labels::OutputOutsideFiltersLabel;

	return true;
}

bool FPCGExUberFilterElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(UberFilter)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		Context->NumPairs = Context->MainPoints->Pairs.Num();

		if (Settings->Mode == EPCGExUberFilterMode::Partition)
		{
			Context->Inside->Pairs.Init(nullptr, Context->NumPairs);
			Context->Outside->Pairs.Init(nullptr, Context->NumPairs);
		}

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
			return Context->CancelExecution(TEXT("Could not find any points to filter."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	if (Settings->Mode == EPCGExUberFilterMode::Write)
	{
		Context->MainPoints->StageOutputs();
	}
	else
	{
		Context->Inside->PruneNullEntries(true);
		Context->Outside->PruneNullEntries(true);

		uint64& Mask = Context->OutputData.InactiveOutputPinBitmask;
		if (!Context->Inside->StageOutputs())
		{
			Mask |= 1ULL << 0;
		}
		if (!Context->Outside->StageOutputs())
		{
			Mask |= 1ULL << 1;
		}
	}

	return Context->TryComplete();
}

namespace PCGExUberFilter
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExUberFilter::Process);

		// Must be set before process for filters
		PointDataFacade->bSupportsScopedGet = Context->bScopedAttributeGet;

		if (!IProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, Settings->GetMainDataInitializationPolicy())

		bUsePicks = PCGExPickers::GetPicks(Context->PickerFactories, PointDataFacade, Picks);

		if (Settings->Mode == EPCGExUberFilterMode::Write)
		{
			Results = Settings->ResultDetails;
			Results.Init(PointDataFacade);
		}
		else
		{
			PCGExArrayHelpers::InitArray(PointFilterCache, PointDataFacade->GetNum());
		}

		StartParallelLoopForPoints(PCGExData::EIOSide::In);

		return true;
	}

	void FProcessor::PrepareLoopScopesForPoints(const TArray<PCGExMT::FScope>& Loops)
	{
		if (!Results.bEnabled)
		{
			const int32 MaxRange = PCGExMT::FScope::GetMaxRange(Loops);

			IndicesInside = MakeShared<PCGExMT::TScopedArray<int32>>(Loops);
			IndicesInside->Reserve(MaxRange);

			IndicesOutside = MakeShared<PCGExMT::TScopedArray<int32>>(Loops);
			IndicesOutside->Reserve(MaxRange);
		}
	}

	void FProcessor::ProcessPoints(const PCGExMT::FScope& Scope)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGEx::UberFilter::ProcessPoints);

		PointDataFacade->Fetch(Scope);
		FilterScope(Scope);

		if (bUsePicks)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				if (!Picks.Contains(Index))
				{
					PointFilterCache[Index] = Settings->UnpickedFallback == EPCGExFilterFallback::Pass;
				}
			}
		}

		if (Settings->bSwap)
		{
			PCGEX_SCOPE_LOOP(Index)
			{
				PointFilterCache[Index] = !PointFilterCache[Index];
			}
		}

		if (!Results.bEnabled)
		{
			TArray<int32>& IndicesInsideRef = IndicesInside->Get_Ref(Scope);
			TArray<int32>& IndicesOutsideRef = IndicesOutside->Get_Ref(Scope);

			PCGEX_SCOPE_LOOP(Index)
			{
				const int8 bPass = PointFilterCache[Index];

				if (bPass)
				{
					IndicesInsideRef.Add(Index);
				}
				else
				{
					IndicesOutsideRef.Add(Index);
				}

				if (bPass)
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

			Results.Write(Scope, PointFilterCache);
		}
	}

	TSharedPtr<PCGExData::FPointIO> FProcessor::CreateIO(const TSharedRef<PCGExData::FPointIOCollection>& InCollection, const PCGExData::EIOInit InitMode) const
	{
		TSharedPtr<PCGExData::FPointIO> NewPointIO = PCGExData::NewPointIO(PointDataFacade->Source, InCollection->OutputPin);

		if (!NewPointIO->InitializeOutput(InitMode))
		{
			return nullptr;
		}

		InCollection->Pairs[BatchIndex] = NewPointIO;
		return NewPointIO;
	}

	void FProcessor::OnPointsProcessingComplete()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExUberFilterProcessor::CompleteWork);

		if (Settings->Mode == EPCGExUberFilterMode::Write)
		{
			const bool bHasAnyPass = NumInside != 0;
			const bool bAllPass = NumInside == PointDataFacade->GetNum();
			if (bHasAnyPass && Settings->bTagIfAnyPointPassed)
			{
				PointDataFacade->Source->Tags->AddRaw(Settings->HasAnyPointPassedTag);
			}
			if (bAllPass && Settings->bTagIfAllPointsPassed)
			{
				PointDataFacade->Source->Tags->AddRaw(Settings->AllPointsPassedTag);
			}
			if (!bHasAnyPass && Settings->bTagIfNoPointPassed)
			{
				PointDataFacade->Source->Tags->AddRaw(Settings->NoPointPassedTag);
			}


			PointDataFacade->WriteFastest(TaskManager);
			return;
		}

		if (NumInside == 0 || NumOutside == 0)
		{
			if (NumInside == 0)
			{
				if (!Settings->bOutputDiscardedElements)
				{
					return;
				}
				Outside = CreateIO(Context->Outside.ToSharedRef(), PCGExData::EIOInit::Forward);
				if (!Outside)
				{
					return;
				}
				if (Settings->bTagIfNoPointPassed)
				{
					Outside->Tags->AddRaw(Settings->NoPointPassedTag);
				}
			}
			else
			{
				Inside = CreateIO(Context->Inside.ToSharedRef(), PCGExData::EIOInit::Forward);
				if (!Inside)
				{
					return;
				}
				if (Settings->bTagIfAnyPointPassed)
				{
					Inside->Tags->AddRaw(Settings->HasAnyPointPassedTag);
				}
				if (Settings->bTagIfAllPointsPassed)
				{
					Inside->Tags->AddRaw(Settings->AllPointsPassedTag);
				}
			}
			return;
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		TArray<int32> Indices;
		PCGExArrayHelpers::InitArray(Indices, NumPoints);

		TArray<int32> ReadIndices;
		IndicesInside->Collapse(ReadIndices);

		Inside = CreateIO(Context->Inside.ToSharedRef(), PCGExData::EIOInit::New);
		if (!Inside)
		{
			return;
		}

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Inside->GetOut(), ReadIndices.Num(), Inside->GetAllocations());
		Inside->InheritProperties(ReadIndices, Inside->GetAllocations());

		if (Settings->bTagIfAnyPointPassed)
		{
			Inside->Tags->AddRaw(Settings->HasAnyPointPassedTag);
		}

		if (!Settings->bOutputDiscardedElements)
		{
			return;
		}

		ReadIndices.Reset();
		IndicesOutside->Collapse(ReadIndices);
		Outside = CreateIO(Context->Outside.ToSharedRef(), PCGExData::EIOInit::New);

		if (!Outside)
		{
			return;
		}

		PCGExPointArrayDataHelpers::SetNumPointsAllocated(Outside->GetOut(), ReadIndices.Num(), Outside->GetAllocations());
		Outside->InheritProperties(ReadIndices, Outside->GetAllocations());
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
