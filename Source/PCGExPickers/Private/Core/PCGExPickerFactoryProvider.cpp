// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExPickerFactoryProvider.h"

#include "Data/PCGExData.h"

#define LOCTEXT_NAMESPACE "PCGExCreatePicker"
#define PCGEX_NAMESPACE CreatePicker

PCG_DEFINE_TYPE_INFO(FPCGExDataTypeInfoPicker, UPCGExPickerFactoryData)

void UPCGExPickerFactoryData::AddPicks(const int32 InNum, TSet<int32>& OutPicks) const
{
}

PCGExFactories::EPreparationResult UPCGExPickerFactoryData::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	PCGExFactories::EPreparationResult Result = Super::Prepare(InContext, TaskManager);
	if (Result != PCGExFactories::EPreparationResult::Success)
	{
		return Result;
	}
	return InitInternalData(InContext);
}

bool UPCGExPickerFactoryData::RequiresInputs() const
{
	return false;
}

PCGExFactories::EPreparationResult UPCGExPickerFactoryData::InitInternalData(FPCGExContext* InContext)
{
	return PCGExFactories::EPreparationResult::Success;
}

TArray<FPCGPinProperties> UPCGExPickerFactoryProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	return PinProperties;
}

UPCGExFactoryData* UPCGExPickerFactoryProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	return Super::CreateFactory(InContext, InFactory);
}

namespace PCGExPickers
{
	bool GetPicks(const TArray<TObjectPtr<const UPCGExPickerFactoryData>>& Factories, const TSharedPtr<PCGExData::FFacade>& InFacade, TSet<int32>& OutPicks)
	{
		if (Factories.IsEmpty())
		{
			return false;
		}

		const int32 Num = InFacade->GetNum();
		for (const TObjectPtr<const UPCGExPickerFactoryData>& Op : Factories)
		{
			Op->AddPicks(Num, OutPicks);
		}
		return true;
	}

	bool GetSortedPicks(const TArray<TObjectPtr<const UPCGExPickerFactoryData>>& Factories, const int32 InNum, TArray<int32>& OutPicks)
	{
		OutPicks.Reset();

		if (InNum <= 0)
		{
			return false;
		}

		TSet<int32> UniquePicks;
		for (const TObjectPtr<const UPCGExPickerFactoryData>& Op : Factories)
		{
			Op->AddPicks(InNum, UniquePicks);
		}

		// -1 is what an out-of-bounds pick sanitizes to.
		UniquePicks.Remove(-1);

		OutPicks = UniquePicks.Array();
		OutPicks.Sort();
		return !OutPicks.IsEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
