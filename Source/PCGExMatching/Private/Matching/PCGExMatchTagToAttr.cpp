// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchTagToAttr.h"

#include "PCGExVersion.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactoryData.h"


#define LOCTEXT_NAMESPACE "PCGExMatchTagToAttr"
#define PCGEX_NAMESPACE MatchTagToAttr

void FPCGExMatchTagToAttrConfig::Init()
{
	FPCGAttributePropertyInputSelector S;
	//S.Update(CandidateAttributeName.ToString());
	//CandidateAttributeName_Sanitized = S.GetAttributeName();

	FPCGExMatchRuleConfigBase::Init();
}

#if WITH_EDITOR
void FPCGExMatchTagToAttrConfig::ApplyDeprecation()
{
	TagNameValue.Update(TagNameInput_DEPRECATED, TagNameAttribute_DEPRECATED, TagName_DEPRECATED);
}

void FPCGExMatchTagToAttrConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("TagName")), FName(TEXT("TagNameValue")), FName(TEXT("Constant")), FName(TEXT("Tag Name")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("TagNameAttribute")), FName(TEXT("TagNameValue")), FName(TEXT("Attribute")), FName(TEXT("Tag Name (Attr)")));
}
#endif

bool FPCGExMatchTagToAttr::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	if (!FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources))
	{
		return false;
	}

	TArray<FPCGExTaggedData>& MatchableSourcesRef = *InMatchableSources.Get();

	TagNameGetters.Reserve(MatchableSourcesRef.Num());
	if (Config.TagNameValue.Input == EPCGExInputValueType::Attribute)
	{
		for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
		{
			TSharedPtr<PCGExData::TAttributeBroadcaster<FString>> Getter = MakeShared<PCGExData::TAttributeBroadcaster<FString>>();

			if (!Getter->PrepareForSingleFetch(Config.TagNameValue.Attribute, TaggedData))
			{
				PCGEX_LOG_INVALID_ATTR_C(InContext, Tag Name, Config.TagNameValue.Attribute)
				return false;
			}

			TagNameGetters.Add(Getter);
		}
	}

	if (!Config.bDoValueMatch)
	{
		return true;
	}

	switch (Config.ValueType)
	{
	case EPCGExComparisonDataType::Numeric:
		NumGetters.Reserve(MatchableSourcesRef.Num());
		for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
		{
			TSharedPtr<PCGExData::TAttributeBroadcaster<double>> Getter = MakeShared<PCGExData::TAttributeBroadcaster<double>>();

			if (!Getter->PrepareForSingleFetch(Config.ValueAttribute, TaggedData))
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Value, Config.ValueAttribute)
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

			if (!Getter->PrepareForSingleFetch(Config.ValueAttribute, TaggedData))
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Value, Config.ValueAttribute)
				return false;
			}

			StrGetters.Add(Getter);
		}
		break;
	}

	return true;
}

bool FPCGExMatchTagToAttr::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	const FString TestTagName = TagNameGetters.IsEmpty() ? Config.TagNameValue.Constant : TagNameGetters[InTargetElement.IO]->FetchSingle(InTargetElement, TEXT(""));

	if (!Config.bDoValueMatch)
	{
		return PCGExCompare::HasMatchingTags(InCandidate.GetTags(), TestTagName, Config.NameMatch) ? !Config.bInvert : Config.bInvert;
	}


	TArray<TSharedPtr<PCGExData::IDataValue>> TagValues;
	if (!PCGExCompare::GetMatchingValueTags(InCandidate.GetTags(), TestTagName, Config.NameMatch, TagValues))
	{
		return Config.bInvert;
	}

	if (Config.ValueType == EPCGExComparisonDataType::Numeric)
	{
		const double OperandBNumeric = NumGetters[InTargetElement.IO]->FetchSingle(InTargetElement, 0);
		for (const TSharedPtr<PCGExData::IDataValue>& TagValue : TagValues)
		{
			if (!PCGExCompare::Compare(Config.NumericComparison, TagValue, OperandBNumeric, Config.Tolerance))
			{
				return Config.bInvert;
			}
		}
	}
	else
	{
		const FString OperandBString = StrGetters[InTargetElement.IO]->FetchSingle(InTargetElement, TEXT(""));
		for (const TSharedPtr<PCGExData::IDataValue>& TagValue : TagValues)
		{
			if (!PCGExCompare::Compare(Config.StringComparison, TagValue, OperandBString))
			{
				return Config.bInvert;
			}
		}
	}

	return !Config.bInvert;
}

bool UPCGExMatchTagToAttrFactory::WantsPoints() const
{
	if (Config.TagNameValue.Input == EPCGExInputValueType::Attribute && !PCGExMetaHelpers::IsDataDomainAttribute(Config.TagNameValue.Attribute))
	{
		return true;
	}
	if (!Config.bDoValueMatch)
	{
		return false;
	}
	return !PCGExMetaHelpers::IsDataDomainAttribute(Config.ValueAttribute);
}

PCGEX_MATCH_RULE_BOILERPLATE(TagToAttr)

#if WITH_EDITOR
void UPCGExCreateMatchTagToAttrSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateMatchTagToAttrSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExCreateMatchTagToAttrSettings::GetDisplayName() const
{
	FString TagSourceStr = Config.TagNameValue.Input == EPCGExInputValueType::Constant ? Config.TagNameValue.Constant : TEXT("Tag \"") + Config.TagNameValue.Attribute.ToString() + TEXT("\"");

	if (Config.bDoValueMatch)
	{
		TagSourceStr += TEXT("::Value ") + PCGExCompare::ToString(Config.NameMatch);

		if (Config.ValueType == EPCGExComparisonDataType::Numeric)
		{
			TagSourceStr += PCGExCompare::ToString(Config.NumericComparison);
		}
		else
		{
			TagSourceStr += PCGExCompare::ToString(Config.StringComparison);
		}

		TagSourceStr += TEXT("Target' @") + PCGExMetaHelpers::GetSelectorDisplayName(Config.ValueAttribute);
	}
	else
	{
		TagSourceStr += PCGExCompare::ToString(Config.NameMatch) + TEXT("Target' @");
		TagSourceStr += Config.TagNameValue.Input == EPCGExInputValueType::Constant ? Config.TagNameValue.Constant : Config.TagNameValue.Attribute.ToString();
	}

	return TagSourceStr;
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
