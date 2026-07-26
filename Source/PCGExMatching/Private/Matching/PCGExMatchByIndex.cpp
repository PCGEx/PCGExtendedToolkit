// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchByIndex.h"

#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExDataHelpers.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Math/PCGExMath.h"


#define LOCTEXT_NAMESPACE "PCGExMatchByIndex"
#define PCGEX_NAMESPACE MatchByIndex

void FPCGExMatchByIndexConfig::Init()
{
	FPCGExMatchRuleConfigBase::Init();
}

bool FPCGExMatchByIndex::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	if (!FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources))
	{
		return false;
	}

	TArray<FPCGExTaggedData>& MatchableSourcesRef = *InMatchableSources.Get();

	bIsIndex = Config.IndexAttribute.GetSelection() == EPCGAttributePropertySelection::ExtraProperty && Config.IndexAttribute.GetExtraProperty() == EPCGExtraProperties::Index;

	if (!bIsIndex && Config.Source == EPCGExMatchByIndexSource::Target)
	{
		IndexGetters.Reserve(MatchableSourcesRef.Num());
		for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
		{
			TSharedPtr<PCGExData::TAttributeBroadcaster<int32>> Getter = MakeShared<PCGExData::TAttributeBroadcaster<int32>>();

			if (!Getter->PrepareForSingleFetch(Config.IndexAttribute, TaggedData))
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Index Attribute, Config.IndexAttribute)
				return false;
			}

			IndexGetters.Add(Getter);
		}
	}

	return true;
}

bool FPCGExMatchByIndex::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	int32 IndexValue = -1;
	int32 OtherIndex = -1;

	if (Config.Source == EPCGExMatchByIndexSource::Target)
	{
		if (bIsIndex)
		{
			IndexValue = InTargetElement.Data ? InTargetElement.Index : InTargetElement.IO;
		}
		else
		{
			IndexValue = IndexGetters[InTargetElement.IO]->FetchSingle(InTargetElement, -1);
		}

		OtherIndex = InCandidate.Index;

		IndexValue = PCGExMath::SanitizeIndex(IndexValue, InMatchingScope.GetNumCandidates() - 1, Config.IndexSafety);
	}
	else
	{
		if (!PCGExData::Helpers::TryReadDataValue<int32>(Context, InCandidate.Data, Config.IndexAttribute, IndexValue))
		{
			return Config.bInvert;
		}
		OtherIndex = InTargetElement.Data ? InTargetElement.Index : InTargetElement.IO;

		IndexValue = PCGExMath::SanitizeIndex(IndexValue, InTargetElement.Data ? InTargetElement.Data->GetNumPoints() - 1 : MatchableSources->Num() - 1, Config.IndexSafety);
	}

	if (IndexValue == -1 || OtherIndex == -1)
	{
		return Config.bInvert;
	}
	const bool bResult = IndexValue == OtherIndex;
	return Config.bInvert ? !bResult : bResult;
}

bool UPCGExMatchByIndexFactory::WantsPoints() const
{
	return !PCGExMetaHelpers::IsDataDomainAttribute(Config.IndexAttribute);
}

PCGEX_MATCH_RULE_BOILERPLATE(ByIndex)

#if WITH_EDITOR
FString UPCGExCreateMatchByIndexSettings::GetDisplayName() const
{
	if (Config.Source == EPCGExMatchByIndexSource::Target)
	{
		return PCGExCommon::FlagInvertLabel(
			TEXT("Target' ") + PCGExMetaHelpers::GetSelectorDisplayName(Config.IndexAttribute) + TEXT(" == Input Index"),
			Config.bInvert);
	}
	return PCGExCommon::FlagInvertLabel(
		TEXT("Input' ") + PCGExMetaHelpers::GetSelectorDisplayName(Config.IndexAttribute) + TEXT(" == Target Index"),
		Config.bInvert);
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
