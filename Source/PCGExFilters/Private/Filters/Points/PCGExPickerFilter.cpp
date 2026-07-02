// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExPickerFilter.h"


#include "Core/PCGExPickerFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExPickerFilterDefinition"
#define PCGEX_NAMESPACE PCGExPickerFilterDefinition

bool UPCGExPickerFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	return PCGExFactories::GetInputFactories(InContext, PCGExPickers::Labels::SourcePickersLabel, PickerFactories, {FPCGExDataTypeInfoPicker::AsId()});
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExPickerFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FPickerFilter>(this);
}

bool PCGExPointFilter::FPickerFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	for (const TObjectPtr<const UPCGExPickerFactoryData>& FactoryData : TypedFilterFactory->PickerFactories)
	{
		FactoryData->AddPicks(InPointDataFacade->GetNum(), Picks);
	}

	return true;
}

bool PCGExPointFilter::FPickerFilter::Test(const int32 PointIndex) const
{
	return TypedFilterFactory->Config.bInvert ? !Picks.Contains(PointIndex) : Picks.Contains(PointIndex);
}

bool PCGExPointFilter::FPickerFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	if (!ParentCollection)
	{
		return false;
	}

	const int32 NumEntries = ParentCollection->Num();
	for (const TObjectPtr<const UPCGExPickerFactoryData>& FactoryData : TypedFilterFactory->PickerFactories)
	{
		TSet<int32> TempPicks;
		FactoryData->AddPicks(NumEntries, TempPicks);
		if (TempPicks.Contains(IO->IOIndex))
		{
			return !TypedFilterFactory->Config.bInvert;
		}
	}

	return TypedFilterFactory->Config.bInvert;
}

TArray<FPCGPinProperties> UPCGExPickerFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_FACTORIES(PCGExPickers::Labels::SourcePickersLabel, "Pickers", Required, FPCGExDataTypeInfoPicker::AsId())
	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(Picker)

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
