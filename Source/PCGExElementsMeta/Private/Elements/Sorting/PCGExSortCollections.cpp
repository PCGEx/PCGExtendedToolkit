// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Elements/Sorting/PCGExSortCollections.h"
#include "PCGModule.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Sorting/PCGExPointSorter.h"
#include "Sorting/PCGExSortingDetails.h"

#define LOCTEXT_NAMESPACE "PCGExSortCollectionsElement"
#define PCGEX_NAMESPACE SortCollections

bool UPCGExSortCollectionsSettings::HasDynamicPins() const
{
	return true;
}

TArray<FPCGPinProperties> UPCGExSortCollectionsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_ANY(GetMainInputPin(), "Inputs", Required)
	PCGExSorting::DeclareSortingRulesInputs(PinProperties, EPCGPinStatus::Required);
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExSortCollectionsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_ANY(GetMainOutputPin(), "Sorted collections.", Normal)
	return PinProperties;
}

PCGEX_INITIALIZE_ELEMENT(SortCollections)

bool FPCGExSortCollectionsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExPointsProcessorElement::Boot(InContext))
	{
		return false;
	}

	PCGEX_CONTEXT_AND_SETTINGS(SortCollections)

	TArray<FPCGExSortRuleConfig> RuleConfigs = PCGExSorting::GetSortingRules(InContext, PCGExSorting::Labels::SourceSortingRules);

	if (RuleConfigs.IsEmpty())
	{
		PCGEX_LOG_MISSING_INPUT(Context, FTEXT("Missing sorting rules."))
		return false;
	}

	Context->Datas = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());

	Context->Sorter = MakeShared<PCGExSorting::FSorter>(RuleConfigs);
	Context->Sorter->SortDirection = Settings->SortDirection;

	return Context->Sorter->Init(InContext, Context->Datas);
}

bool FPCGExSortCollectionsElement::AdvanceWork(FPCGExContext* InContext, const UPCGExSettings* InSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExSortCollectionsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(SortCollections)
	PCGEX_EXECUTION_CHECK

	PCGEX_ON_INITIAL_EXECUTION
	{
		TArray<int32> Indices;
		PCGExArrayHelpers::ArrayOfIndices(Indices, Context->Datas.Num());

		// Stable: ties keep arrival order.
		Indices.StableSort([&](const int32 A, const int32 B)
		{
			return Context->Sorter->SortData(A, B);
		});

		for (const int32 i : Indices)
		{
			Context->OutputData.TaggedData.Add(Context->Datas[i]);
		}

		Context->Done();
	}

	return Context->TryComplete();
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
