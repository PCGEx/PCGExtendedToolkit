// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "UObject/Object.h"

#include "PCGExFillControlRunningAverage.generated.h"

class UPCGSettings;
class UPCGNode;

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigRunningAverage : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigRunningAverage()
		: FPCGExFillControlConfigBase()
	{
		bSupportSteps = false;
		WindowSizeAttribute_DEPRECATED.Update("WindowSize");
		Operand.Update("$Position.Z");
	}

	/** Window Size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Window Size"))
	FPCGExInputShorthandSelectorInteger32Abs WindowSizeValue = FPCGExInputShorthandSelectorInteger32Abs(FName("WindowSize"), 10, false);

	/** Tolerance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Tolerance"))
	FPCGExInputShorthandNameDoubleAbs ToleranceValue = FPCGExInputShorthandNameDoubleAbs(FName("Tolerance"), 10, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType WindowSizeInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector WindowSizeAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 WindowSize_DEPRECATED = 10;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType ToleranceInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FName ToleranceAttribute_DEPRECATED = FName("Tolerance");

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Tolerance_DEPRECATED = 10;

#pragma endregion

	/** The property that will be averaged and checked against candidates -- will be broadcasted to a `double`. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Operand;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * 
 */
class FPCGExFillControlRunningAverage : public FPCGExFillControlOperation
{
	friend class UPCGExFillControlsFactoryRunningAverage;

public:
	virtual bool PrepareForDiffusions(FPCGExContext* InContext, const TSharedPtr<PCGExFloodFill::FFillControlsHandler>& InHandler) override;

	virtual bool ChecksCapture() const override
	{
		return false;
	}

	virtual bool ChecksProbe() const override
	{
		return false;
	}

	virtual bool ChecksCandidate() const override
	{
		return true;
	}

	virtual bool IsValidCandidate(const PCGExFloodFill::FDiffusion* Diffusion, const PCGExFloodFill::FCandidate& From, const PCGExFloodFill::FCandidate& Candidate) override;

	virtual bool WantsTravelStack() const override
	{
		return true; // Walks the travel stack to average values along the path
	}

protected:
	TSharedPtr<PCGExDetails::TSettingValue<int32>> WindowSize;
	TSharedPtr<PCGExDetails::TSettingValue<double>> Tolerance;
	TSharedPtr<PCGExData::TBuffer<double>> Operand;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fc-running-average"))
class UPCGExFillControlsFactoryRunningAverage : public UPCGExFillControlsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExFillControlConfigRunningAverage Config;

	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params")
class UPCGExFillControlsRunningAverageProviderSettings : public UPCGExFillControlsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FillControlsRunningAverage, "Fill Control : Running Average", "Ignore candidates which attribute value isn't within the given tolerance of a running average.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Control Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFillControlConfigRunningAverage Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
