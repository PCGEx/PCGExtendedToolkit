// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsHandler.h"
#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "UObject/Object.h"

#include "PCGExFillControlHxScoring.generated.h"

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigHeuristicsScoring : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigHeuristicsScoring()
		: FPCGExFillControlConfigBase()
	{
		bSupportSource = false;
		bSupportSteps = false;
		Steps = static_cast<uint8>(EPCGExFloodFillControlStepsFlags::None); // Scoring only, no validation
	}

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** What components are used for scoring points. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable, Bitmask, BitmaskEnum="/Script/PCGExElementsFloodFill.EPCGExFloodFillHeuristicFlags"))
	uint8 Scoring = static_cast<uint8>(EPCGExFloodFillHeuristicFlags::LocalScore);

	/** Weight multiplier applied to scores from this control. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = 0))
	double ScoreWeight = 1.0;
};

/**
 * Scoring control that computes heuristic scores for candidates.
 * Replaces the core heuristics integration with a more modular approach.
 */
class FPCGExFillControlHeuristicsScoring : public FPCGExFillControlOperation
{
	friend class UPCGExFillControlsFactoryHxScoring;

public:
	virtual bool DoesScoring() const override
	{
		return true;
	}

	virtual bool PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler) override;
	virtual void ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate) override;

	virtual bool WantsTravelStack() const override
	{
		return true; // Heuristics edge scoring receives the travel stack
	}

protected:
	TSharedPtr<PCGExHeuristics::FHandler> HeuristicsHandler;
	/** Resolved during PrepareForDiffusions -- ScoreCandidate runs in parallel and must not hit the lazy lookup */
	const PCGExClusters::FNode* RoamingGoal = nullptr;

	bool bUseLocalScore = false;
	bool bUseGlobalScore = false;
	bool bUsePreviousScore = false;
	double ScoreWeight = 1.0;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFillControlsFactoryHxScoring : public UPCGExFillControlsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExFillControlConfigHeuristicsScoring Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> HeuristicsFactories;

	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fc-heuristics-scoring"))
class UPCGExFillControlsHeuristicsScoringProviderSettings : public UPCGExFillControlsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FillControlsHeuristicsScoring, "Fill Control : Heuristics Scoring", "Compute and accumulate heuristic scores for diffusion candidates.", FName(GetDisplayName()))

	virtual FLinearColor GetNodeTitleColor() const override
	{
		return PCGEX_NODE_COLOR_BLEND(FillControl, Heuristics);
	}
#endif
	//~End UPCGSettings

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;

public:
	/** Control Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFillControlConfigHeuristicsScoring Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
