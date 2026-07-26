// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Collections/PCGExTagValueFilter.h"

#include "Data/PCGExPointIO.h"

#define LOCTEXT_NAMESPACE "PCGExCompareFilterDefinition"
#define PCGEX_NAMESPACE CompareFilterDefinition

TSharedPtr<PCGExPointFilter::IFilter> UPCGExTagValueFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FTagValueFilter>(this);
}

bool PCGExPointFilter::FTagValueFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	bool bResult = false;

	if (TArray<TSharedPtr<PCGExData::IDataValue>> TagValues;
		PCGExCompare::GetMatchingValueTags(IO->Tags, TypedFilterFactory->Config.Tag, TypedFilterFactory->Config.Match, TagValues))
	{
		// AND mode: assume true, break on first failure. OR mode: override below if any matched.
		bool bAtLeastOneMatch = false;
		bResult = true;

		if (TypedFilterFactory->Config.ValueType == EPCGExComparisonDataType::Numeric)
		{
			double B = TypedFilterFactory->Config.NumericOperandB;

			for (const TSharedPtr<PCGExData::IDataValue>& TagValue : TagValues)
			{
				if (!PCGExCompare::Compare(TypedFilterFactory->Config.NumericComparison, TagValue, B, TypedFilterFactory->Config.Tolerance))
				{
					bResult = false;
					break;
				}
				bAtLeastOneMatch = true;
			}
		}
		else
		{
			FString B = TypedFilterFactory->Config.StringOperandB;
			for (const TSharedPtr<PCGExData::IDataValue>& TagValue : TagValues)
			{
				if (!PCGExCompare::Compare(TypedFilterFactory->Config.StringComparison, TagValue, B))
				{
					bResult = false;
					break;
				}
				bAtLeastOneMatch = true;
			}
		}

		// In OR mode, override AND-failure: pass if at least one tag value matched.
		if (TypedFilterFactory->Config.MultiMatch == EPCGExFilterGroupMode::OR && bAtLeastOneMatch)
		{
			bResult = true;
		}
	}

	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

PCGEX_CREATE_FILTER_FACTORY(TagValue)

#if WITH_EDITOR
FString UPCGExTagValueFilterProviderSettings::GetDisplayName() const
{
	if (Config.ValueType == EPCGExComparisonDataType::Numeric)
	{
		FString DisplayName = Config.Tag + TEXT(" ") + PCGExCompare::ToString(Config.NumericComparison);
		DisplayName += FString::Printf(TEXT("%.1f"), Config.NumericOperandB);
		DisplayName += Config.MultiMatch == EPCGExFilterGroupMode::OR ? TEXT(" (Any)") : TEXT(" (All)");
		return DisplayName;
	}
	FString DisplayName = Config.Tag + TEXT(" ") + PCGExCompare::ToString(Config.StringComparison);
	DisplayName += FString::Printf(TEXT(" %s"), *Config.StringOperandB);
	DisplayName += Config.MultiMatch == EPCGExFilterGroupMode::OR ? TEXT(" (Any)") : TEXT(" (All)");
	
	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
