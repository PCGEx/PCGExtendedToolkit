// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "FillControls/PCGExFillControlHxBudget.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"
#include "Details/PCGExSettingsDetails.h"

PCGEX_SETTING_VALUE_IMPL(FPCGExFillControlConfigHeuristicsBudget, MaxBudget, double, MaxBudgetInput, MaxBudgetAttribute, MaxBudget)

bool FPCGExFillControlHeuristicsBudget::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryHxBudget* TypedFactory = Cast<UPCGExFillControlsFactoryHxBudget>(Factory);

	BudgetSource = TypedFactory->Config.BudgetSource;

	// Initialize budget setting value
	MaxBudget = TypedFactory->Config.GetValueSettingMaxBudget();
	if (!MaxBudget->Init(GetSourceFacade()))
	{
		return false;
	}

	if (TypedFactory->HeuristicsFactories.IsEmpty())
	{
		// No heuristics provided - we'll use PathDistance as fallback for scoring
		return true;
	}

	// Build our own heuristics handler
	HeuristicsHandler = PCGExHeuristics::FHandler::CreateHandler(
		TypedFactory->Config.HeuristicScoreMode,
		InContext,
		InHandler->VtxDataFacade,
		InHandler->EdgeDataFacade,
		TypedFactory->HeuristicsFactories);

	HeuristicsHandler->PrepareForCluster(InHandler->Cluster);
	HeuristicsHandler->CompleteClusterPreparation();
	// Diffusion re-scores candidates throughout the fill -- always worth baking static edge scores.
	HeuristicsHandler->BakeStaticEdgeScores();
	// Resolve the roaming goal once, single-threaded -- diffusions run in parallel and the lazy
	// lookup both races and re-runs a full closest-edge scan per racing thread.
	RoamingGoal = HeuristicsHandler->GetRoamingGoal();

	return true;
}

void FPCGExFillControlHeuristicsBudget::ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate)
{
	if (!HeuristicsHandler)
	{
		// No heuristics - use edge distance as score
		OutCandidate.PathScore = From.PathScore + OutCandidate.Distance;
		OutCandidate.Score += OutCandidate.Distance;
		return;
	}

	const PCGExClusters::FNode& FromNode = *From.Node;
	const PCGExClusters::FNode& ToNode = *OutCandidate.Node;
	const PCGExClusters::FNode& SeedNode = *Diffusion->SeedNode;

	const double EdgeScore = HeuristicsHandler->GetEdgeScore(
		FromNode, ToNode,
		*Cluster->GetEdge(OutCandidate.Link),
		SeedNode, *RoamingGoal,
		nullptr, Diffusion->TravelStack.Get());

	// Always accumulate for budget tracking
	OutCandidate.PathScore = From.PathScore + EdgeScore;
	OutCandidate.Score += EdgeScore;
}

bool FPCGExFillControlHeuristicsBudget::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	const double CurrentValue = GetBudgetValue(Candidate);
	const double Budget = MaxBudget->Read(GetSettingsIndex(Diffusion));
	return CurrentValue <= Budget;
}

double FPCGExFillControlHeuristicsBudget::GetBudgetValue(const PCGExFloodFill::FCandidate& Candidate) const
{
	switch (BudgetSource)
	{
	case EPCGExFloodFillBudgetSource::PathScore:
		return Candidate.PathScore;
	case EPCGExFloodFillBudgetSource::CompositeScore:
		return Candidate.Score;
	case EPCGExFloodFillBudgetSource::PathDistance:
		return Candidate.PathDistance;
	}
	return 0;
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryHxBudget::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlHeuristicsBudget)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryHxBudget::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	for (const TObjectPtr<const UPCGExHeuristicsFactoryData>& HFactory : HeuristicsFactories)
	{
		HFactory->RegisterBuffersDependencies(InContext, FacadePreloader);
	}
}

TArray<FPCGPinProperties> UPCGExFillControlsHeuristicsBudgetProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics used for cost calculation.", Normal, FPCGExDataTypeInfoHeuristics::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExFillControlsHeuristicsBudgetProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryHxBudget* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryHxBudget>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	Super::CreateFactory(InContext, NewFactory);

	// Heuristics are optional for budget - can use PathDistance if not provided
	PCGExFactories::GetInputFactories(InContext, PCGExHeuristics::Labels::SourceHeuristicsLabel, NewFactory->HeuristicsFactories, {FPCGExDataTypeInfoHeuristics::AsId()}, false);

	return NewFactory;
}

#if WITH_EDITOR
FString UPCGExFillControlsHeuristicsBudgetProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control : Heuristics"), TEXT("FC × HX"));
}
#endif
