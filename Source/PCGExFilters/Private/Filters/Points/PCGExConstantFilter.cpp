// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExConstantFilter.h"

#include "Containers/PCGExManagedObjects.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

bool UPCGExConstantFilterFactory::Init(FPCGExContext* InContext)
{
	return Super::Init(InContext);
}

TSharedPtr<PCGExPointFilter::IFilter> UPCGExConstantFilterFactory::CreateFilter() const
{
	PCGEX_MAKE_SHARED(Filter, PCGExPointFilter::FConstantFilter, this)
	return Filter;
}

bool PCGExPointFilter::FConstantFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}
	ConstantValue = TypedFilterFactory->Config.bInvert ? !TypedFilterFactory->Config.Value : TypedFilterFactory->Config.Value;
	return true;
}

bool PCGExPointFilter::FConstantFilter::Test(const int32 PointIndex) const
{
	return ConstantValue;
}

bool PCGExPointFilter::FConstantFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	return ConstantValue;
}

bool PCGExPointFilter::FConstantFilter::Test(const PCGExData::FProxyPoint& Point) const
{
	return ConstantValue;
}

#if WITH_EDITOR
TArray<FPCGPreConfiguredSettingsInfo> UPCGExConstantFilterProviderSettings::GetPreconfiguredInfo() const
{
	TArray<FPCGPreConfiguredSettingsInfo> Infos;
	Infos.Emplace(0, FTEXT("Always fail"));
	Infos.Emplace(1, FTEXT("Always pass"));
	return Infos;
}
#endif

void UPCGExConstantFilterProviderSettings::ApplyPreconfiguredSettings(const FPCGPreConfiguredSettingsInfo& PreconfigureInfo)
{
	Super::ApplyPreconfiguredSettings(PreconfigureInfo);
	Config.Value = PreconfigureInfo.PreconfiguredIndex == 1;
	// Presets state an absolute outcome; a leftover inversion would silently negate it.
	Config.bInvert = false;
}

PCGEX_CREATE_FILTER_FACTORY(Constant)

#if WITH_EDITOR
FString UPCGExConstantFilterProviderSettings::GetDisplayName() const
{
	// Reflect the evaluated constant, inversion included.
	return (Config.bInvert ? !Config.Value : Config.Value) ? TEXT("Always pass") : TEXT("Always fail");
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
