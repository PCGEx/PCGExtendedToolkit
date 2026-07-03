// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Details/PCGExSettingsMacros.h"
#include "UObject/Object.h"

#include "PCGExFillControlRunningAverage.generated.h"

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigRunningAverage : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigRunningAverage()
		: FPCGExFillControlConfigBase()
	{
		bSupportSteps = false;
		WindowSizeAttribute.Update("WindowSize");
		Operand.Update("$Position.Z");
	}

	/** Whether the window size is a constant or from an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExInputValueType WindowSizeInput = EPCGExInputValueType::Constant;

	/** Window Size Attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Window Size (Attr)", EditCondition="WindowSizeInput != EPCGExInputValueType::Constant", EditConditionHides))
	FPCGAttributePropertyInputSelector WindowSizeAttribute;

	/** Window Size Constant */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Window Size", EditCondition="WindowSizeInput == EPCGExInputValueType::Constant", EditConditionHides, ClampMin=1))
	int32 WindowSize = 10;

	PCGEX_SETTING_VALUE_DECL(WindowSize, int32)

	/** Whether the tolerance is a constant or from an attribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_NotOverridable))
	EPCGExInputValueType ToleranceInput = EPCGExInputValueType::Constant;

	/** Tolerance Attribute */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Tolerance (Attr)", EditCondition="ToleranceInput != EPCGExInputValueType::Constant", EditConditionHides))
	FName ToleranceAttribute = FName("Tolerance");

	/** Tolerance Constant */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Tolerance", EditCondition="ToleranceInput == EPCGExInputValueType::Constant", EditConditionHides, ClampMin=0))
	double Tolerance = 10;

	PCGEX_SETTING_VALUE_DECL(Tolerance, double)

	/** The property that will be averaged and checked against candidates -- will be broadcasted to a `double`. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Operand;
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
