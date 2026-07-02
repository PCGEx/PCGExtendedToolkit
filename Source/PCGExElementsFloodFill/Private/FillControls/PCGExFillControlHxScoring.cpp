// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "FillControls/PCGExFillControlHxScoring.h"

#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"

bool FPCGExFillControlHeuristicsScoring::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryHxScoring* TypedFactory = Cast<UPCGExFillControlsFactoryHxScoring>(Factory);

	// Extract scoring flags
	bUseLocalScore = (TypedFactory->Config.Scoring & static_cast<uint8>(EPCGExFloodFillHeuristicFlags::LocalScore)) != 0;
	bUseGlobalScore = (TypedFactory->Config.Scoring & static_cast<uint8>(EPCGExFloodFillHeuristicFlags::GlobalScore)) != 0;
	bUsePreviousScore = (TypedFactory->Config.Scoring & static_cast<uint8>(EPCGExFloodFillHeuristicFlags::PreviousScore)) != 0;
	ScoreWeight = TypedFactory->Config.ScoreWeight;

	if (TypedFactory->HeuristicsFactories.IsEmpty())
	{
		// No heuristics provided - this is valid, just won't contribute scores
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

void FPCGExFillControlHeuristicsScoring::ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate)
{
	if (!HeuristicsHandler)
	{
		return;
	}

	const PCGExClusters::FNode& FromNode = *From.Node;
	const PCGExClusters::FNode& ToNode = *OutCandidate.Node;
	const PCGExClusters::FNode& SeedNode = *Diffusion->SeedNode;

	if (bUseLocalScore || bUsePreviousScore)
	{
		const double LocalScore = HeuristicsHandler->GetEdgeScore(
			FromNode, ToNode,
			*Cluster->GetEdge(OutCandidate.Link),
			SeedNode, *RoamingGoal,
			nullptr, Diffusion->TravelStack.Get());

		if (bUsePreviousScore)
		{
			OutCandidate.PathScore += LocalScore;
			OutCandidate.Score += From.PathScore * ScoreWeight;
		}

		if (bUseLocalScore)
		{
			OutCandidate.Score += LocalScore * ScoreWeight;
		}
	}

	if (bUseGlobalScore)
	{
		OutCandidate.Score += HeuristicsHandler->GetGlobalScore(FromNode, SeedNode, ToNode) * ScoreWeight;
	}
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryHxScoring::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlHeuristicsScoring)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryHxScoring::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	for (const TObjectPtr<const UPCGExHeuristicsFactoryData>& HFactory : HeuristicsFactories)
	{
		HFactory->RegisterBuffersDependencies(InContext, FacadePreloader);
	}
}

TArray<FPCGPinProperties> UPCGExFillControlsHeuristicsScoringProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics used for scoring.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExFillControlsHeuristicsScoringProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryHxScoring* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryHxScoring>();
	PCGEX_FORWARD_FILLCONTROL_FACTORY
	Super::CreateFactory(InContext, NewFactory);

	if (!PCGExFactories::GetInputFactories(InContext, PCGExHeuristics::Labels::SourceHeuristicsLabel, NewFactory->HeuristicsFactories, {FPCGExDataTypeInfoHeuristics::AsId()}))
	{
		InContext->ManagedObjects->Destroy(NewFactory);
		return nullptr;
	}

	return NewFactory;
}

#if WITH_EDITOR
FString UPCGExFillControlsHeuristicsScoringProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control : Heuristics"), TEXT("FC × HX"));
}
#endif
