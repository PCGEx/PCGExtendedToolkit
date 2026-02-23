// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Filters/Points/PCGExDataMatchFilter.h"

#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExDataMatcher.h"
#include "Helpers/PCGExMatchingHelpers.h"

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
	const TSharedPtr<PCGExData::FPointIOCollection> Targets = MakeShared<PCGExData::FPointIOCollection>(InContext, PCGExCommon::Labels::SourceTargetsLabel, PCGExData::EIOInit::NoInit, true);
	if (Targets->IsEmpty()) { return PCGExFactories::EPreparationResult::MissingData; }

	// Build facades and keep them alive for tag data lifetime
	TargetFacades.Reserve(Targets->Pairs.Num());
	for (int32 i = 0; i < Targets->Pairs.Num(); i++)
	{
		TargetFacades.Add(MakeShared<PCGExData::FFacade>(Targets->Pairs[i].ToSharedRef()));
	}

	// Setup matching details from config mode
	MatchingDetails.Mode = Config.Mode;

	// Initialize data matcher with target facades
	DataMatcher = MakeShared<PCGExMatching::FDataMatcher>();
	DataMatcher->SetDetails(&MatchingDetails);

	if (!DataMatcher->Init(InContext, TargetFacades, false))
	{
		DataMatcher.Reset();
		return PCGExFactories::EPreparationResult::MissingData;
	}

	return Super::Prepare(InContext, TaskManager);
}

void UPCGExDataMatchFilterFactory::BeginDestroy()
{
	Super::BeginDestroy();
}

#pragma endregion

#pragma region FDataMatchFilter

bool PCGExPointFilter::FDataMatchFilter::Init(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade)
{
	if (!IFilter::Init(InContext, InPointDataFacade)) { return false; }

	if (!TypedFilterFactory->DataMatcher)
	{
		bCollectionTestResult = !TypedFilterFactory->Config.bInvert;
		return true;
	}

	const FPCGExTaggedData Candidate = InPointDataFacade->Source->GetTaggedData();
	PCGExMatching::FScope Scope(TypedFilterFactory->DataMatcher->GetNumSources(), true);

	TArray<int32> Matches;
	TypedFilterFactory->DataMatcher->GetMatchingSourcesIndices(Candidate, Scope, Matches);

	const bool bResult = !Matches.IsEmpty();
	bCollectionTestResult = TypedFilterFactory->Config.bInvert ? !bResult : bResult;

	return true;
}

bool PCGExPointFilter::FDataMatchFilter::Test(const int32 PointIndex) const
{
	return bCollectionTestResult;
}

bool PCGExPointFilter::FDataMatchFilter::Test(const TSharedPtr<PCGExData::FPointIO>& IO, const TSharedPtr<PCGExData::FPointIOCollection>& ParentCollection) const
{
	if (!TypedFilterFactory->DataMatcher) { return !TypedFilterFactory->Config.bInvert; }

	const FPCGExTaggedData Candidate = IO->GetTaggedData();
	PCGExMatching::FScope Scope(TypedFilterFactory->DataMatcher->GetNumSources(), true);

	TArray<int32> Matches;
	TypedFilterFactory->DataMatcher->GetMatchingSourcesIndices(Candidate, Scope, Matches);

	const bool bResult = !Matches.IsEmpty();
	return TypedFilterFactory->Config.bInvert ? !bResult : bResult;
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
	if (Config.bInvert) { DisplayName += TEXT(" (Inverted)"); }
	return DisplayName;
}
#endif

#pragma endregion

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
