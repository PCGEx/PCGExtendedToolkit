// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Sorting/PCGExSortingDetails.h"

#include "Data/PCGExDataTags.h"
#include "Data/PCGExDataValue.h"
#include "Data/PCGExPointIO.h"
#include "Sorting/PCGExSortingRuleProvider.h"

#define LOCTEXT_NAMESPACE "PCGExModularSortPoints"
#define PCGEX_NAMESPACE ModularSortPoints

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE

namespace PCGExSorting
{
	void DeclareSortingRulesInputs(TArray<FPCGPinProperties>& PinProperties, const EPCGPinStatus InStatus)
	{
		FPCGPinProperties& Pin = PinProperties.Emplace_GetRef(Labels::SourceSortingRules, FPCGExDataTypeInfoSortRule::AsId());
		PCGEX_PIN_TOOLTIP("Plug sorting rules here. Order is defined by each rule' priority value, in ascending order.")
		Pin.PinStatus = InStatus;
	}

	TArray<FPCGExSortRuleConfig> GetSortingRules(FPCGExContext* InContext, const FName InLabel)
	{
		TArray<FPCGExSortRuleConfig> OutRules;
		TArray<TObjectPtr<const UPCGExSortingRule>> Factories;
		if (!PCGExFactories::GetInputFactories(InContext, InLabel, Factories, {FPCGExDataTypeInfoSortRule::AsId()}, false))
		{
			return OutRules;
		}

		for (const UPCGExSortingRule* Factory : Factories)
		{
			OutRules.Add(Factory->Config);
		}

		return OutRules;
	}
}
