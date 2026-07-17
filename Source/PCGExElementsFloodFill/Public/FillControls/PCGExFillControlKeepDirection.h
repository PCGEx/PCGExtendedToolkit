// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExFillControlOperation.h"
#include "Core/PCGExFillControlsFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "Details/PCGExSettingsMacros.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "PCGExFillControlKeepDirection.generated.h"

class UPCGSettings;
class UPCGNode;

USTRUCT(BlueprintType)
struct FPCGExFillControlConfigKeepDirection : public FPCGExFillControlConfigBase
{
	GENERATED_BODY()

	FPCGExFillControlConfigKeepDirection()
		: FPCGExFillControlConfigBase()
	{
		bSupportSteps = false;
	}

	/** Window Size. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Window Size"))
	FPCGExInputShorthandSelectorInteger32Abs WindowSizeValue = FPCGExInputShorthandSelectorInteger32Abs(FName("WindowSize"), 1, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType WindowSizeInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector WindowSizeAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 WindowSize_DEPRECATED = 1;

#pragma endregion

	/** Hash comparison settings */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExVectorHashComparisonDetails HashComparisonDetails = FPCGExVectorHashComparisonDetails(0.1);

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * 
 */
class FPCGExFillControlKeepDirection : public FPCGExFillControlOperation
{
	friend class UPCGExFillControlsFactoryKeepDirection;

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
		return true; // Walks the travel stack to compare against the path's running direction
	}

protected:
	FPCGExVectorHashComparisonDetails HashComparisonDetails;
	TSharedPtr<PCGExDetails::TSettingValue<int32>> WindowSize;
	TSharedPtr<PCGExDetails::TSettingValue<double>> DistanceLimit;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExFillControlsFactoryKeepDirection : public UPCGExFillControlsFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExFillControlConfigKeepDirection Config;

	virtual TSharedPtr<FPCGExFillControlOperation> CreateOperation(FPCGExContext* InContext) const override;

	virtual void RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="pathfinding/cluster-flood-fill/fc-keep-direction"))
class UPCGExFillControlsKeepDirectionProviderSettings : public UPCGExFillControlsFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(FillControlsKeepDirection, "Fill Control : Keep Direction", "Stop fill after a certain number of vtx have been captured.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	/** Control Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExFillControlConfigKeepDirection Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
