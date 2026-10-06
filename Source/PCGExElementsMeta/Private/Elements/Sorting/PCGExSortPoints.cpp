// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#include "Elements/Sorting/PCGExSortPoints.h"


#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Elements/Sorting/PCGExModularSortPoints.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Sorting/PCGExPointSorter.h"


#define LOCTEXT_NAMESPACE "PCGExSortPoints"
#define PCGEX_NAMESPACE SortPoints

#if WITH_EDITOR
void UPCGExSortPointsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	for (FPCGExSortRuleConfig& Config : Rules)
	{
		Config.UpdateUserFacingInfos();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FPCGElementPtr UPCGExSortPointsBaseSettings::CreateElement() const
{
	return MakeShared<FPCGExSortPointsBaseElement>();
}

PCGExData::EIOInit UPCGExSortPointsBaseSettings::GetMainDataInitializationPolicy() const
{
	return PCGExData::EIOInit::Duplicate;
}

bool UPCGExSortPointsBaseSettings::GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const
{
	return true;
}

bool UPCGExSortPointsSettings::GetSortingRules(FPCGExContext* InContext, TArray<FPCGExSortRuleConfig>& OutRules) const
{
	if (Rules.IsEmpty())
	{
		return false;
	}
	for (const FPCGExSortRuleConfig& Config : Rules)
	{
		OutRules.Add(Config);
	}
	return true;
}

PCGEX_ELEMENT_BATCH_POINT_IMPL(SortPoints)

bool FPCGExSortPointsBaseElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSortPointsElement::Execute);

	PCGEX_CONTEXT(SortPoints)
	PCGEX_SETTINGS(SortPointsBase)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		TArray<FPCGExSortRuleConfig> RuleConfigs;
		if (!Settings->GetSortingRules(Context, RuleConfigs))
		{
			return Context->CancelExecution(TEXT("No attributes to sort over."));
		}

		if (!Context->StartBatchProcessingPoints(
			[&](const TSharedPtr<PCGExData::FPointIO>& Entry)
			{
				return true;
			},
			[&](const TSharedPtr<PCGExPointsMT::IBatch>& NewBatch)
			{
				NewBatch->bPrefetchData = true;
			}))
		{
			return Context->CancelExecution(TEXT("Could not find any points to sort."));
		}
	}

	PCGEX_POINTS_BATCH_PROCESSING(PCGExCommon::States::State_Done)

	Context->MainPoints->StageOutputs();

	return Context->TryComplete();
}

namespace PCGExSortPoints
{
	void FProcessor::RegisterBuffersDependencies(PCGExData::FFacadePreloader& FacadePreloader)
	{
		TProcessor::RegisterBuffersDependencies(FacadePreloader);

		TArray<FPCGExSortRuleConfig> RuleConfigs;
		Settings->GetSortingRules(ExecutionContext, RuleConfigs);

		Sorter = MakeShared<PCGExSorting::FSorter>(Context, PointDataFacade, RuleConfigs);
		Sorter->SortDirection = Settings->SortDirection;
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InTaskManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExSortPoints::Process);

		if (!TProcessor::Process(InTaskManager))
		{
			return false;
		}

		PCGEX_INIT_IO(PointDataFacade->Source, PCGExData::EIOInit::Duplicate)

		if (!Sorter->Init(Context))
		{
			PCGE_LOG_C(Warning, GraphAndLog, ExecutionContext, FTEXT("Some dataset have no valid sorting rules, they won't be sorted."));
			return false;
		}

		const int32 NumPoints = PointDataFacade->GetNum();
		TArray<int32> Order;
		PCGExArrayHelpers::ArrayOfIndices(Order, NumPoints);

		Sorter->SortIndices(Order, NumPoints);

		PointDataFacade->Source->InheritPoints(Order, 0);

		return true;
	}

	void FProcessor::CompleteWork()
	{
		IProcessor::CompleteWork();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
