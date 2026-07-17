// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "FillControls/PCGExFillControlHxThreshold.h"

#include "PCGExVersion.h"
#include "Clusters/PCGExCluster.h"
#include "Containers/PCGExHashLookup.h"
#include "Containers/PCGExManagedObjects.h"
#include "Details/PCGExSettingsDetails.h"
#include "UObject/ObjectMacros.h"

#if WITH_EDITOR
void FPCGExFillControlConfigHeuristicsThreshold::ApplyDeprecation()
{
	ThresholdValue.Update(ThresholdInput_DEPRECATED, ThresholdAttribute_DEPRECATED, Threshold_DEPRECATED);
}

void FPCGExFillControlConfigHeuristicsThreshold::RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const
{
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("Threshold")), FName(TEXT("ThresholdValue")), FName(TEXT("Constant")), FName(TEXT("Threshold")));
	PCGExDeprecation::RenameShorthandOverridePin(InSettings, InOutNode, FName(TEXT("ThresholdAttribute")), FName(TEXT("ThresholdValue")), FName(TEXT("Attribute")), FName(TEXT("Threshold (Attr)")));
}
#endif

bool FPCGExFillControlHeuristicsThreshold::PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler)
{
	if (!FPCGExFillControlOperation::PrepareForDiffusions(InContext, InHandler))
	{
		return false;
	}

	const UPCGExFillControlsFactoryHxThreshold* TypedFactory = Cast<UPCGExFillControlsFactoryHxThreshold>(Factory);

	ThresholdSource = TypedFactory->Config.ThresholdSource;
	Comparison = TypedFactory->Config.Comparison;
	Tolerance = TypedFactory->Config.Tolerance;

	// Initialize threshold setting value
	Threshold = TypedFactory->Config.ThresholdValue.GetValueSetting();
	Threshold->bRegisterConsumable &= TypedFactory->bCleanupConsumableAttributes;
	if (!Threshold->Init(GetSourceFacade()))
	{
		return false;
	}

	if (TypedFactory->HeuristicsFactories.IsEmpty())
	{
		PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Heuristics Threshold fill control requires at least one heuristic."));
		return false;
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

double FPCGExFillControlHeuristicsThreshold::ComputeEdgeScore(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate) const
{
	return HeuristicsHandler->GetEdgeScore(
		*From.Node, *Candidate.Node,
		*Cluster->GetEdge(Candidate.Link),
		*Diffusion->SeedNode, *RoamingGoal,
		nullptr, Diffusion->TravelStack.Get());
}

void FPCGExFillControlHeuristicsThreshold::ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate)
{
	if (!HeuristicsHandler)
	{
		return;
	}

	const double EdgeScore = ComputeEdgeScore(Diffusion, From, OutCandidate);

	// Accumulate scores for path tracking
	OutCandidate.PathScore = From.PathScore + EdgeScore;
	OutCandidate.Score += EdgeScore;
}

bool FPCGExFillControlHeuristicsThreshold::IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate)
{
	double Value = 0;
	switch (ThresholdSource)
	{
	case EPCGExFloodFillThresholdSource::EdgeScore:
		// Recomputed rather than cached: this op instance is shared by diffusions running in
		// parallel, so a score stashed in ScoreCandidate could belong to another thread's candidate.
		if (HeuristicsHandler)
		{
			Value = ComputeEdgeScore(Diffusion, From, Candidate);
		}
		break;
	case EPCGExFloodFillThresholdSource::GlobalScore:
		if (HeuristicsHandler)
		{
			Value = HeuristicsHandler->GetGlobalScore(*From.Node, *Diffusion->SeedNode, *Candidate.Node);
		}
		break;
	case EPCGExFloodFillThresholdSource::ScoreDelta:
		Value = Candidate.Score - From.Score;
		break;
	}

	const double ThresholdValue = Threshold->Read(GetSettingsIndex(Diffusion));

	return PCGExCompare::Compare(Comparison, Value, ThresholdValue, Tolerance);
}

TSharedPtr<FPCGExFillControlOperation> UPCGExFillControlsFactoryHxThreshold::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(FillControlHeuristicsThreshold)
	PCGEX_FORWARD_FILLCONTROL_OPERATION
	return NewOperation;
}

void UPCGExFillControlsFactoryHxThreshold::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
	Super::RegisterBuffersDependencies(InContext, FacadePreloader);

	for (const TObjectPtr<const UPCGExHeuristicsFactoryData>& HFactory : HeuristicsFactories)
	{
		HFactory->RegisterBuffersDependencies(InContext, FacadePreloader);
	}
}

TArray<FPCGPinProperties> UPCGExFillControlsHeuristicsThresholdProviderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_FACTORIES(PCGExHeuristics::Labels::SourceHeuristicsLabel, "Heuristics used for threshold calculation.", Required, FPCGExDataTypeInfoHeuristics::AsId())
	return PinProperties;
}

UPCGExFactoryData* UPCGExFillControlsHeuristicsThresholdProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExFillControlsFactoryHxThreshold* NewFactory = InContext->ManagedObjects->New<UPCGExFillControlsFactoryHxThreshold>();
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
void UPCGExFillControlsHeuristicsThresholdProviderSettings::PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.RenamePins(this, InOutNode);
	}

	Super::PCGExApplyDeprecationBeforeUpdatePins(InOutNode, InputPins, OutputPins);
}

void UPCGExFillControlsHeuristicsThresholdProviderSettings::PCGExApplyDeprecation(UPCGNode* InOutNode)
{
	PCGEX_IF_VERSION_LOWER(1, 76, 10)
	{
		Config.ApplyDeprecation();
	}

	Super::PCGExApplyDeprecation(InOutNode);
}

FString UPCGExFillControlsHeuristicsThresholdProviderSettings::GetDisplayName() const
{
	return GetDefaultNodeTitle().ToString().Replace(TEXT("PCGEx | Fill Control : Heuristics"), TEXT("FC × HX"));
}
#endif
