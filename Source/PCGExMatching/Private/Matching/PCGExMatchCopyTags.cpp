// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchCopyTags.h"

#include "Data/PCGExDataTags.h"
#include "Data/PCGExPointIO.h"


#define LOCTEXT_NAMESPACE "PCGExMatchCopyTags"
#define PCGEX_NAMESPACE MatchCopyTags

bool FPCGExMatchCopyTags::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	return FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources);
}

bool FPCGExMatchCopyTags::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	if (InTargetElement.Data)
	{
		return true;
	}

	const FPCGExTaggedData* TaggedData = MatchableSources->GetData() + InTargetElement.IO;

	if (const TSharedPtr<PCGExData::FTags> Tags = TaggedData->GetTags();
		const TSharedPtr<PCGExData::FTags> CandidateTags = InCandidate.GetTags())
	{
		CandidateTags->Append(Tags.ToSharedRef());
	}

	return true;
}

PCGEX_MATCH_RULE_BOILERPLATE(CopyTags)

#if WITH_EDITOR
FString UPCGExCreateMatchCopyTagsSettings::GetDisplayName() const
{
	return TEXT("Copy Tags");
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
