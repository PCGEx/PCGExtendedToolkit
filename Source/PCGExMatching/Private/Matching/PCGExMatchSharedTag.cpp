// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchSharedTag.h"

#include "PCGExVersion.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExMatchSharedTag"
#define PCGEX_NAMESPACE MatchSharedTag

void FPCGExMatchSharedTagConfig::Init()
{
	FPCGAttributePropertyInputSelector S;
	//S.Update(CandidateAttributeName.ToString());
	//CandidateAttributeName_Sanitized = S.GetAttributeName();

	FPCGExMatchRuleConfigBase::Init();
}

#if WITH_EDITOR
void FPCGExMatchSharedTagConfig::ApplyDeprecation()
{
	TagNameValue.Update(TagNameInput_DEPRECATED, TagNameAttribute_DEPRECATED, TagName_DEPRECATED);
}

void FPCGExMatchSharedTagConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("TagName")), FName(TEXT("TagNameValue")), FName(TEXT("Constant")), FName(TEXT("Tag Name")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("TagNameAttribute")), FName(TEXT("TagNameValue")), FName(TEXT("Attribute")), FName(TEXT("Tag Name (Attr)")));
}
#endif

bool FPCGExMatchSharedTag::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	if (!FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources))
	{
		return false;
	}

	TArray<FPCGExTaggedData>& MatchableSourcesRef = *InMatchableSources.Get();

	Tags.Reserve(MatchableSourcesRef.Num());

	// Always store tags for all modes
	for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
	{
		Tags.Add(TaggedData.Tags);
	}

	// Only setup attribute getters for Specific mode with attribute input
	if (Config.Mode == EPCGExTagMatchMode::Specific && Config.TagNameValue.Input == EPCGExInputValueType::Attribute)
	{
		TagNameGetters.Reserve(MatchableSourcesRef.Num());
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

	return true;
}

bool FPCGExMatchSharedTag::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	TSharedPtr<PCGExData::FTags> TargetTags = Tags[InTargetElement.IO].Pin();
	if (!TargetTags)
	{
		return Config.bInvert;
	}

	TSharedPtr<PCGExData::FTags> CandidateTags = InCandidate.GetTags();
	if (!CandidateTags)
	{
		return Config.bInvert;
	}

	bool bResult = false;

	switch (Config.Mode)
	{
	case EPCGExTagMatchMode::Specific:
	{
		FString TestTagName = TagNameGetters.IsEmpty() ? Config.TagNameValue.Constant : TagNameGetters[InTargetElement.IO]->FetchSingle(InTargetElement, TEXT(""));
		bool bDoValueMatch = Config.bDoValueMatch;

		// If the raw string in the tag:value format, enforce value check
		if (TSharedPtr<PCGExData::IDataValue> Value = PCGExData::TryGetValueFromTag(TestTagName, TestTagName))
		{
			bDoValueMatch = true;
		}

		TSharedPtr<PCGExData::IDataValue> TargetValue = TargetTags->GetValue(TestTagName);
		TSharedPtr<PCGExData::IDataValue> SourceValue = CandidateTags->GetValue(TestTagName);

		if (bDoValueMatch)
		{
			if (!TargetValue || !SourceValue)
			{
				bResult = false;
			}
			else
			{
				bResult = TargetValue->SameValue(SourceValue);
			}
		}
		else if (TargetValue && SourceValue)
		{
			bResult = true;
		}
		else if (TargetValue || SourceValue)
		{
			bResult = false;
		}
		else
		{
			bResult = TargetTags->RawTags.Contains(TestTagName) && CandidateTags->RawTags.Contains(TestTagName);
		}
	}
	break;

	case EPCGExTagMatchMode::AnyShared:
	{
		// Check if ANY tag is shared
		if (Config.bMatchTagValues)
		{
			// Check value tags
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : TargetTags->ValueTags)
			{
				if (TSharedPtr<PCGExData::IDataValue> CandidateValue = CandidateTags->GetValue(Pair.Key))
				{
					if (Pair.Value->SameValue(CandidateValue))
					{
						bResult = true;
						break;
					}
				}
			}
		}
		else
		{
			// Check raw tags
			for (const FString& Tag : TargetTags->RawTags)
			{
				if (CandidateTags->RawTags.Contains(Tag))
				{
					bResult = true;
					break;
				}
			}

			// Check value tag names (ignoring values)
			if (!bResult)
			{
				for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : TargetTags->ValueTags)
				{
					if (CandidateTags->ValueTags.Contains(Pair.Key))
					{
						bResult = true;
						break;
					}
				}
			}
		}
	}
	break;

	case EPCGExTagMatchMode::AllShared:
	{
		// Check if ALL tags from candidate exist in target
		bResult = true;

		if (Config.bMatchTagValues)
		{
			// All candidate value tags must exist with same value in target
			for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : CandidateTags->ValueTags)
			{
				TSharedPtr<PCGExData::IDataValue> TargetValue = TargetTags->GetValue(Pair.Key);
				if (!TargetValue || !Pair.Value->SameValue(TargetValue))
				{
					bResult = false;
					break;
				}
			}

			// All candidate raw tags must exist in target
			if (bResult)
			{
				for (const FString& Tag : CandidateTags->RawTags)
				{
					if (!TargetTags->RawTags.Contains(Tag))
					{
						bResult = false;
						break;
					}
				}
			}
		}
		else
		{
			// All candidate raw tags must exist in target
			for (const FString& Tag : CandidateTags->RawTags)
			{
				if (!TargetTags->RawTags.Contains(Tag))
				{
					bResult = false;
					break;
				}
			}

			// All candidate value tag names must exist in target (ignoring values)
			if (bResult)
			{
				for (const TPair<FString, TSharedPtr<PCGExData::IDataValue>>& Pair : CandidateTags->ValueTags)
				{
					if (!TargetTags->ValueTags.Contains(Pair.Key))
					{
						bResult = false;
						break;
					}
				}
			}
		}

		// Empty candidate tags always match
		if (CandidateTags->RawTags.IsEmpty() && CandidateTags->ValueTags.IsEmpty())
		{
			bResult = true;
		}
	}
	break;
	}

	return Config.bInvert ? !bResult : bResult;
}

bool UPCGExMatchSharedTagFactory::WantsPoints() const
{
	return Config.Mode == EPCGExTagMatchMode::Specific &&
		Config.TagNameValue.Input == EPCGExInputValueType::Attribute &&
		!PCGExMetaHelpers::IsDataDomainAttribute(Config.TagNameValue.Attribute);
}

PCGEX_MATCH_RULE_BOILERPLATE(SharedTag)

#if WITH_EDITOR
void UPCGExCreateMatchSharedTagSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateMatchSharedTagSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExCreateMatchSharedTagSettings::GetDisplayName() const
{
	switch (Config.Mode)
	{
	case EPCGExTagMatchMode::Specific:
	{
		FString NameStr = TEXT("Share ");
		NameStr += Config.TagNameValue.Input == EPCGExInputValueType::Constant ? Config.TagNameValue.Constant : TEXT("Tag \"") + Config.TagNameValue.Attribute.ToString() + TEXT("\"");
		return NameStr;
	}
	case EPCGExTagMatchMode::AnyShared:
		return TEXT("Any Shared Tag");
	case EPCGExTagMatchMode::AllShared:
		return TEXT("All Tags Shared");
	default:
		return TEXT("Shared Tag");
	}
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
