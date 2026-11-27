// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExDataMatchFilter.h"

#include "Core/PCGExMatchRuleFactoryProvider.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactories.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"
#include "Misc/ScopeRWLock.h"

#define LOCTEXT_NAMESPACE "PCGExDataMatchFilterDefinition"
#define PCGEX_NAMESPACE DataMatchFilterDefinition

#pragma region UPCGExDataMatchFilterFactory

TSharedPtr<PCGExPointFilter::IFilter> UPCGExDataMatchFilterFactory::CreateFilter() const
{
	return MakeShared<PCGExPointFilter::FDataMatchFilter>(this);
}

PCGExFactories::EPreparationResult UPCGExDataMatchFilterFactory::Prepare(FPCGExContext* InContext, const TSharedPtr<PCGExMT::FTaskManager>& TaskManager)
{
	// Load targets from pin
	const TSharedPtr<PCGExData::FPointIOCollection> Targets = MakeShared<PCGExData::FPointIOCollection>(InContext, PCGExCommon::Labels::SourceTargetsLabel, PCGExData::EIOInit::NoInit, PCGExData::EIOHandling::Transactional);
	if (Targets->IsEmpty())
	{
		return PCGExFactories::EPreparationResult::MissingData;
	}

	// Build facades and keep them alive for tag data lifetime
	TargetFacades.Reserve(Targets->Pairs.Num());
	for (int32 i = 0; i < Targets->Pairs.Num(); i++)
	{
		TargetFacades.Add(MakeShared<PCGExData::FFacade>(Targets->Pairs[i].ToSharedRef()));
	}

	MatchingDetails.Mode = Config.Mode;

	// Initialize data matcher with target facades
	DataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	DataMatcher->SetDetails(&MatchingDetails);

	if (!DataMatcher->Init(InContext, TargetFacades, false))
	{
		DataMatcher.Reset();
		return PCGExFactories::EPreparationResult::MissingData;
	}

	if (Config.bChainThroughFilteredData)
	{
		if (DataMatcher->WantsRecursion())
		{
			PCGExFactories::GetInputFactories(InContext, PCGExMatching::Labels::SourceMatchRulesLabel, ChainRules, {FPCGExDataTypeInfoMatchRule::AsId()}, false);
		}
		else if (!Config.bQuietChainWarnings)
		{
			PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Chain Through Filtered Data has no effect: no match rule is recursive."));
		}
	}

	return Super::Prepare(InContext, TaskManager);
}

TSharedPtr<const PCGExMatching::FChainReach> UPCGExDataMatchFilterFactory::GetChainReach(FPCGExContext* InContext, const TSharedPtr<PCGExData::FPointIOCollection>& InFilteredData) const
{
	using FChainEntry = TPair<TWeakPtr<PCGExData::FPointIOCollection>, TSharedPtr<const PCGExMatching::FChainReach>>;

	{
		FReadScopeLock ReadScope(ChainReachLock);
		for (const FChainEntry& Entry : ChainReaches)
		{
			if (Entry.Key.Pin() == InFilteredData)
			{
				return Entry.Value;
			}
		}
	}

	TSharedPtr<PCGExMatching::FChainReach> ChainReach;

	{
		FWriteScopeLock WriteScope(ChainReachLock);

		// Weak keys: a new collection at a dead one's address can't match its entry. Drop the dead ones while here.
		ChainReaches.RemoveAll([](const FChainEntry& Entry) { return !Entry.Key.IsValid(); });
		for (const FChainEntry& Entry : ChainReaches)
		{
			if (Entry.Key.Pin() == InFilteredData)
			{
				return Entry.Value;
			}
		}

		TArray<FPCGExTaggedData> FilteredData;
		FilteredData.Reserve(InFilteredData->Num());
		for (const TSharedPtr<PCGExData::FPointIO>& IO : InFilteredData->Pairs)
		{
			FilteredData.Add(IO->GetTaggedData());
		}

		ChainReach = MakeShared<PCGExMatching::FChainReach>();
		if (!ChainReach->Init(DataMatcher.ToSharedRef(), ChainRules, FilteredData))
		{
			ChainReach.Reset();
		}

		ChainReaches.Emplace(InFilteredData, ChainReach);
	}

	// Only the call that built the entry gets here, so each filtered collection warns once.
	if (!ChainReach && !Config.bQuietChainWarnings)
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Chain Through Filtered Data can't chain these inputs: a match rule rejects one of them as a target (a missing attribute, for instance). Only direct matches count."));
	}

	return ChainReach;
}

void UPCGExDataMatchFilterFactory::WarnChainsUnavailable(FPCGExContext* InContext) const
{
	if (!Config.bQuietChainWarnings && !bWarnedChainsUnavailable.exchange(true))
	{
		PCGE_LOG_C(Warning, GraphAndLog, InContext, FTEXT("Chain Through Filtered Data needs a node that filters whole data; only direct matches count here."));
	}
}

#pragma endregion

#pragma region FDataMatchFilter

bool PCGExPointFilter::FDataMatchFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade))
	{
		return false;
	}

	if (!TypedFilterFactory->DataMatcher)
	{
		bCollectionTestResult = !TypedFilterFactory->Config.bInvert;
		return true;
	}

	// Collection hosts chain through Test(IO, ParentCollection) instead.
	if (!HostCollection && !bWillBeUsedWithCollections && TypedFilterFactory->WantsChains())
	{
		TypedFilterFactory->WarnChainsUnavailable(InContext);
	}

	const bool bResult = Matches(*InPointDataFacade->Source, HostCollection);
	bCollectionTestResult = TypedFilterFactory->Config.bInvert ? !bResult : bResult;

	return true;
}

bool PCGExPointFilter::FDataMatchFilter::Test(const int32 PointIndex) const
{
	return bCollectionTestResult;
}

bool PCGExPointFilter::FDataMatchFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	if (!TypedFilterFactory->DataMatcher)
	{
		return !TypedFilterFactory->Config.bInvert;
	}

	const bool bResult = Matches(*IO, ParentCollection);
	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
}

bool PCGExPointFilter::FDataMatchFilter::Matches(PCGExData::FPointIO& InIO, const TSharedPtr<PCGExData::FPointIOCollection>& InFilteredData) const
{
	const FPCGExTaggedData Candidate = InIO.GetTaggedData();

	if (InFilteredData && TypedFilterFactory->WantsChains())
	{
		if (const TSharedPtr<const PCGExMatching::FChainReach> ChainReach = TypedFilterFactory->GetChainReach(InIO.GetContext(), InFilteredData))
		{
			return ChainReach->Reaches(Candidate);
		}
	}

	PCGExMatching::FScope Scope(TypedFilterFactory->DataMatcher->GetNumSources(), true);

	TArray<int32> MatchedSources;
	TypedFilterFactory->DataMatcher->GetMatchingSourcesIndices(Candidate, Scope, MatchedSources);

	return !MatchedSources.IsEmpty();
}

#pragma endregion

#pragma region UPCGExDataMatchFilterProviderSettings

TArray<FPCGPinProperties> UPCGExDataMatchFilterProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINTS(PCGExCommon::Labels::SourceTargetsLabel, TEXT("Target data to match against."), Required)

	FPCGExMatchingDetails TempDetails;
	TempDetails.Mode = Config.Mode;
	PCGExMatching::Helpers::DeclareMatchingRulesInputs(TempDetails, PinProperties);

	return PinProperties;
}

PCGEX_CREATE_FILTER_FACTORY(DataMatch)

#if WITH_EDITOR
FString UPCGExDataMatchFilterProviderSettings::GetDisplayName() const
{
	FString DisplayName = TEXT("Data Match : ");
	DisplayName += Config.Mode == EPCGExMapMatchMode::All ? TEXT("All") : TEXT("Any");

	if (Config.bChainThroughFilteredData)
	{
		DisplayName += TEXT(" (Chained)");
	}

	return PCGExCommon::FlagInvertLabel(DisplayName, Config.bInvert);
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
