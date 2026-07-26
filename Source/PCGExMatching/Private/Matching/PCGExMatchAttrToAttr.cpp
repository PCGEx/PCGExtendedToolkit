// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchAttrToAttr.h"

#include "Containers/PCGExManagedObjects.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointElements.h"
#include "Data/PCGExTaggedData.h"
#include "Helpers/PCGExMetaHelpers.h"

#define LOCTEXT_NAMESPACE "PCGExMatchAttrToAttr"
#define PCGEX_NAMESPACE MatchAttrToAttr

void FPCGExMatchAttrToAttrConfig::Init()
{
	FPCGAttributePropertyInputSelector S;
	S.Update(CandidateAttributeName.ToString());
	CandidateAttributeName_Sanitized = S.GetAttributeName();

	FPCGExMatchRuleConfigBase::Init();
}

bool FPCGExMatchAttrToAttr::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	if (!FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources))
	{
		return false;
	}

	TArray<FPCGExTaggedData>& MatchableSourcesRef = *InMatchableSources.Get();

	switch (Config.Check)
	{
	case EPCGExComparisonDataType::Numeric:
		NumGetters.Reserve(MatchableSourcesRef.Num());
		for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
		{
			TSharedPtr<PCGExData::TAttributeBroadcaster<double>> Getter = MakeShared<PCGExData::TAttributeBroadcaster<double>>();

			if (!Getter->PrepareForSingleFetch(Config.TargetAttributeName, TaggedData))
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, Target Attribute, Config.TargetAttributeName)
				return false;
			}

			NumGetters.Add(Getter);
		}
		break;
	case EPCGExComparisonDataType::String:
		StrGetters.Reserve(MatchableSourcesRef.Num());
		for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
		{
			TSharedPtr<PCGExData::TAttributeBroadcaster<FString>> Getter = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();

			if (!Getter->PrepareForSingleFetch(Config.TargetAttributeName, TaggedData))
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, Target Attribute, Config.TargetAttributeName)
				return false;
			}

			StrGetters.Add(Getter);
		}
		break;
	}

	return true;
}

bool FPCGExMatchAttrToAttr::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	if (Config.Check == EPCGExComparisonDataType::Numeric)
	{
		const double TargetValue = NumGetters[InTargetElement.IO]->FetchSingle(InTargetElement, TNumericLimits<double>::Max());
		double CandidateValue = 0;

		if (!PCGExData::Helpers::TryReadDataValue<double>(Context, InCandidate.Data, Config.CandidateAttributeName_Sanitized, CandidateValue))
		{
			return Config.bInvert;
		}

		const bool bResult =
			Config.bSwapOperands
			? PCGExCompare::Compare(Config.NumericComparison, TargetValue, CandidateValue, Config.Tolerance)
			: PCGExCompare::Compare(Config.NumericComparison, CandidateValue, TargetValue, Config.Tolerance);

		return Config.bInvert ? !bResult : bResult;
	}

	const FString TargetValue = StrGetters[InTargetElement.IO]->FetchSingle(InTargetElement, TEXT(""));
	FString CandidateValue = TEXT("");

	if (!PCGExData::Helpers::TryReadDataValue<FString>(Context, InCandidate.Data, Config.CandidateAttributeName_Sanitized, CandidateValue))
	{
		return Config.bInvert;
	}

	const bool bResult =
		Config.bSwapOperands
		? PCGExCompare::Compare(Config.StringComparison, TargetValue, CandidateValue)
		: PCGExCompare::Compare(Config.StringComparison, CandidateValue, TargetValue);

	return Config.bInvert ? !bResult : bResult;
}

bool UPCGExMatchAttrToAttrFactory::WantsPoints() const
{
	return !PCGExMetaHelpers::IsDataDomainAttribute(Config.TargetAttributeName);
}

PCGEX_MATCH_RULE_BOILERPLATE(AttrToAttr)

#if WITH_EDITOR
FString UPCGExCreateMatchAttrToAttrSettings::GetDisplayName() const
{
	if (Config.Check == EPCGExComparisonDataType::Numeric)
	{
		return PCGExCommon::FlagInvertLabel(
			Config.bSwapOperands ? Config.TargetAttributeName.ToString() + PCGExCompare::ToString(Config.NumericComparison) + Config.CandidateAttributeName.ToString() : Config.CandidateAttributeName.ToString() + PCGExCompare::ToString(Config.NumericComparison) + Config.TargetAttributeName.ToString(),
			Config.bInvert);
	}
	
	return PCGExCommon::FlagInvertLabel(
		Config.bSwapOperands ? Config.TargetAttributeName.ToString() + PCGExCompare::ToString(Config.StringComparison) + Config.CandidateAttributeName.ToString() : Config.CandidateAttributeName.ToString() + PCGExCompare::ToString(Config.StringComparison) + Config.TargetAttributeName.ToString(),
		Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
