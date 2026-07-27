// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Collections/PCGExTagCheckFilter.h"


#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExTagCheckFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FTagCheckFilter>(this);
}

bool PCGExPointFilter::FTagCheckFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	bool bResult = PCGExCompare::HasMatchingTags(IO->Tags, TypedFilterFactory->Config.Tag, TypedFilterFactory->Config.Match, TypedFilterFactory->Config.bStrict);
	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

PCGEX_CREATE_FILTER_FACTORY(TagCheck)

#if WITH_EDITOR
FString UPCGExTagCheckFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Tags that... ") + PCGExCompare::ToString(Config.Match);
	DisplayName += FString::Printf(TEXT(" \"%s\""), *Config.Tag);
	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
