// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Matching/PCGExMatchRandom.h"

#include "PCGExVersion.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExRandomHelpers.h"


#define LOCTEXT_NAMESPACE "PCGExMatchRandom"
#define PCGEX_NAMESPACE MatchRandom

FPCGExMatchRandomConfig::FPCGExMatchRandomConfig()
	: FPCGExMatchRuleConfigBase()
{
	ThresholdAttribute_DEPRECATED.Update("@Data.Threshold");
}

#if WITH_EDITOR
void FPCGExMatchRandomConfig::ApplyDeprecation()
{
	ThresholdValue.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, Threshold_DEPRECATED);
}

void FPCGExMatchRandomConfig::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Threshold")), FName(TEXT("ThresholdValue")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("ThresholdValue")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}
#endif

bool FPCGExMatchRandom::PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources)
{
	if (!FPCGExMatchRuleOperation::PrepareForMatchableSources(InContext, InMatchableSources))
	{
		return false;
	}

	TArray<FPCGExTaggedData>& MatchableSourcesRef = *InMatchableSources.Get();

	if (Config.ThresholdValue.Input == EPCGExInputValueType::Attribute)
	{
		ThresholdGetters.Reserve(MatchableSourcesRef.Num());
		for (const FPCGExTaggedData& TaggedData : MatchableSourcesRef)
		{
			TSharedPtr<PCGExData::TAttributeBroadcaster<double>> Getter = MakeShared<PCGExData::TAttributeBroadcaster<double>>();

			if (!Getter->PrepareForSingleFetch(Config.ThresholdValue.Attribute, TaggedData))
			{
				PCGEX_LOG_INVALID_SELECTOR_C(InContext, Index Attribute, Config.ThresholdValue.Attribute)
				return false;
			}

			ThresholdGetters.Add(Getter);
		}
	}

	return true;
}

bool FPCGExMatchRandom::Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const
{
	const double LocalThreshold = ThresholdGetters.IsEmpty() ? Config.ThresholdValue.Constant : ThresholdGetters[InTargetElement.IO]->FetchSingle(InTargetElement, Config.ThresholdValue.Constant);
	const float RandomValue = FRandomStream(PCGExRandomHelpers::GetRandomStreamFromPoint(Config.RandomSeed + InTargetElement.IO, InCandidate.Index)).GetFraction();
	const bool bResult = Config.bInvertThreshold ? RandomValue <= LocalThreshold : RandomValue >= LocalThreshold;
	return Config.bInvert ? !bResult : bResult;
}

bool UPCGExMatchRandomFactory::WantsPoints() const
{
	return !PCGExMetaHelpers::IsDataDomainAttribute(Config.ThresholdValue.Attribute);
}

PCGEX_MATCH_RULE_BOILERPLATE(Random)

#if WITH_EDITOR
void UPCGExCreateMatchRandomSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExCreateMatchRandomSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExCreateMatchRandomSettings::GetDisplayName() const
{
	return TEXT("Random");
}
#endif

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
