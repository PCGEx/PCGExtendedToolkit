// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExPickerFilter.h"


#include "Core/PCGExPickerFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Misc/ScopeRWLock.h"

#define LOCTEXT_NAMESPACE "PCGExPickerFilterDefinition"
#define PCGEX_NAMESPACE PCGExPickerFilterDefinition

bool UPCGExPickerFilterFactory::Init(FPCGExContext* InContext)
{
	if (!Super::Init(InContext))
	{
		return false;
	}

	return PCGExFactories::GetInputFactories(InContext, PCGExPickers::Labels::SourcePickersLabel, PickerFactories, {PCGExFactories::EType::IndexPicker});
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExPickerFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FPickerFilter>(this);
}

bool UPCGExPickerFilterFactory::IsPicked(const int32 InIndex, const int32 InNum) const
{
	{
		FReadScopeLock ReadScope(SharedPicksLock);
		if (SharedPicksNum == InNum)
		{
			return SharedPicks.Contains(InIndex);
		}
	}

	FWriteScopeLock WriteScope(SharedPicksLock);
	if (SharedPicksNum != InNum)
	{
		SharedPicks.Reset();
		for (const TObjectPtr<const UPCGExPickerFactoryData>& FactoryData : PickerFactories)
		{
			FactoryData->AddPicks(InNum, SharedPicks);
		}
		SharedPicksNum = InNum;
	}

	return SharedPicks.Contains(InIndex);
}

bool PCGExPointFilter::FPickerFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	if (bWillBeUsedWithCollections)
	{
		return true;
	}

	if (HostCollection)
	{
		bTestsDataIndex = true;
		bDataIndexResult = Test(InPointDataFacade->Source, HostCollection);
		return true;
	}

	PCGExPickers::GetPicks(TypedFilterFactory->PickerFactories, InPointDataFacade, Picks);
	return true;
}

bool PCGExPointFilter::FPickerFilter::Test(const int32 PointIndex) const
{
	return bTestsDataIndex ? bDataIndexResult : Picks.Contains(PointIndex) != TypedFilterFactory->Config.bInvert;
}

bool PCGExPointFilter::FPickerFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	if (!ParentCollection)
	{
		return false;
	}

	return TypedFilterFactory->IsPicked(IO->IOIndex, ParentCollection->Num()) != TypedFilterFactory->Config.bInvert;
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
