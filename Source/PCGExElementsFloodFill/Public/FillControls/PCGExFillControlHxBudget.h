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

#include "PCGExFillControlHxBudget.generated.h"

class UPCGSettings;
class UPCGNode;

UENUM(BlueprintType)
enum class EPCGExFloodFillBudgetSource : uint8
{
	PathScore UMETA(DisplayName = "Path Score", ToolTip = "Accumulated heuristic score along path"),
	CompositeScore UMETA(DisplayName = "Composite Score", ToolTip = "Total combined score"),
	PathDistance UMETA(DisplayName = "Path Distance", ToolTip = "Accumulated spatial distance"),
};

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigHeuristicsBudget : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigHeuristicsBudget()
		: FPCGExFillControlConfigBase()
	{
		bSupportSteps = false;
	}

	/** Scoring mode for combining multiple heuristics */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	EPCGExHeuristicScoreMode HeuristicScoreMode = EPCGExHeuristicScoreMode::WeightedAverage;

	/** Maximum accumulated heuristic cost allowed before stopping. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, DisplayName = "Max Budget"))
	FPCGExInputShorthandNameDoubleAbs MaxBudgetValue = FPCGExInputShorthandNameDoubleAbs(FName("MaxBudget"), 100.0, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType MaxBudgetInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName MaxBudgetAttribute_DEPRECATED = FName("MaxBudget");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double MaxBudget_DEPRECATED = 100.0;

#pragma endregion

	/** Which score to track for budget comparison. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_NotOverridable))
	EPCGExFloodFillBudgetSource BudgetSource = EPCGExFloodFillBudgetSource::PathScore;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * Budget control that computes heuristic scores AND stops diffusion when budget is exceeded.
 * Combines scoring and validation in a single control.
 */
class FPCGExFillControlHeuristicsBudget : public FPCGExFillControlOperation
{
	friend class UPCGExFillControlsFactoryHxBudget;

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
	/** Resolved during PrepareForDiffusions -- ScoreCandidate runs in parallel and must not hit the lazy lookup */
	const PCGExClusters::FNode* RoamingGoal = nullptr;
	TSharedPtr<PCGExDetails::TSettingValue<double>> MaxBudget;
	EPCGExFloodFillBudgetSource BudgetSource = EPCGExFloodFillBudgetSource::PathScore;

	double GetBudgetValue(const PCGExFloodFill::FCandidate& Candidate) const;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFillControlsFactoryHxBudget : public UPCGExFillControlsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExFillControlConfigHeuristicsBudget Config;

	UPROPERTY()
	TArray<TObjectPtr<const UPCGExHeuristicsFactoryData>> HeuristicsFactories;

	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fc-heuristics-budget"))
class UPCGExFillControlsHeuristicsBudgetProviderSettings : public UPCGExFillControlsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FillControlsHeuristicsBudget, "Fill Control : Heuristics Budget", "Stop diffusion when accumulated heuristic cost exceeds a budget.", FName(GetDisplayName()))

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
	FPCGExFillControlConfigHeuristicsBudget Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
