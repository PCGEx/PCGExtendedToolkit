// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExHeuristicsHandler.h"
#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Core/PCGExHeuristicsFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "PCGExFillControlHxThreshold.generated.h"

class UPCGSettings;
class UPCGNode;

UENUM(BlueprintType)
enum class EPCGExFloodFillThresholdSource : uint8
{
	EdgeScore UMETA(DisplayName = "Edge Score", ToolTip = "Current edge's heuristic score"),
	GlobalScore UMETA(DisplayName = "Global Score", ToolTip = "Heuristic distance from seed to candidate"),
	ScoreDelta UMETA(DisplayName = "Score Delta", ToolTip = "Change in score from previous candidate"),
};

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigHeuristicsThreshold : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigHeuristicsThreshold()
		: FPCGExFillControlConfigBase()
	{
		bSupportSteps = false;
	}

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** Threshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Threshold"))
	FPCGExInputShorthandNameDouble ThresholdValue = FPCGExInputShorthandNameDouble(FName("Threshold"), 0.5, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType ThresholdInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName ThresholdAttribute_DEPRECATED = FName("Threshold");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Threshold_DEPRECATED = 0.5;

#pragma endregion

	/** Comparison mode. Candidate is valid if: ThresholdSource [Comparison] Threshold */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExComparison Comparison = EPCGExComparison::StrictlySmaller;

	/** Tolerance for near-equal comparisons. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides, ClampMin = 0))
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** What value to compare against the threshold. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFloodFillThresholdSource ThresholdSource = EPCGExFloodFillThresholdSource::EdgeScore;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * Threshold control that stops diffusion when instantaneous heuristic crosses a threshold.
 * Unlike Budget which tracks accumulated cost, this checks single edge/node values.
 */
class FPCGExFillControlHeuristicsThreshold : public FPCGExFillControlOperation
{
	friend class UPCGExFillControlsFactoryHxThreshold;

public:
	virtual bool DoesScoring() const override
	{
		return true;
	}

	virtual bool ChecksCandidate() const override
	{
		return true;
	}

	virtual bool PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler) override;
	virtual void ScoreCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, PCGExFloodFill::FCandidate& OutCandidate) override;
	virtual bool IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate) override;

	virtual bool WantsTravelStack() const override
	{
		return true; // Heuristics edge scoring receives the travel stack
	}

protected:
	TSharedPtr<PCGExHeuristics::FHandler> HeuristicsHandler;
	/** Resolved during PrepareForDiffusions -- candidate scoring runs in parallel and must not hit the lazy lookup */
	const PCGExClusters::FNode* RoamingGoal = nullptr;
	TSharedPtr<PCGExDetails::TSettingValue<double>> Threshold;
	EPCGExFloodFillThresholdSource ThresholdSource = EPCGExFloodFillThresholdSource::EdgeScore;
	EPCGExComparison Comparison = EPCGExComparison::StrictlySmaller;
	double Tolerance = DBL_COMPARE_TOLERANCE;

	/** Shared by ScoreCandidate & IsValidCandidate. This op instance is shared across diffusions
	 * that run in parallel, so per-candidate results must never be cached on the op itself. */
	double ComputeEdgeScore(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFillControlsFactoryHxThreshold : public UPCGExFillControlsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExFillControlConfigHeuristicsThreshold Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> HeuristicsFactories;

	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fc-heuristics-threshold"))
class UPCGExFillControlsHeuristicsThresholdProviderSettings : public UPCGExFillControlsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FillControlsHeuristicsThreshold, "Fill Control : Heuristics Threshold", "Stop diffusion when instantaneous heuristic crosses a threshold.", FName(GetDisplayName()))

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
	FPCGExFillControlConfigHeuristicsThreshold Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
