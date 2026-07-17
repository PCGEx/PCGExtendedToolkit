// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExMatchRuleFactoryProvider.h"
#include "Details/PCGExInputShorthandsDetails.h"
#include "PCGExMatchRandom.generated.h"

class UPCGSettings;
class UPCGNode;

namespace PCGExData
{
	template <typename T>
	class TAttributeBroadcaster;
}

USTRUCT(BlueprintType)
struct FPCGExMatchRandomConfig : public FPCGExMatchRuleConfigBase
{
	GENERATED_BODY()

	FPCGExMatchRandomConfig();

	/** Seed used for random number generation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	int32 RandomSeed = 42;

	/** Pass threshold -- Value is expected to fit within a 0-1 range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Threshold"))
	FPCGExInputShorthandSelectorDouble01 ThresholdValue = FPCGExInputShorthandSelectorDouble01(FString(TEXT("@Data.Threshold")), 0.5, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType ThresholdInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector ThresholdAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	double Threshold_DEPRECATED = 0.5;

#pragma endregion

	/** Invert the threshold comparison (pass becomes fail and vice versa). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	bool bInvertThreshold = false;

#if WITH_EDITOR
	void ApplyDeprecation();
	void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const;
#endif
};

/**
 * 
 */
class FPCGExMatchRandom : public FPCGExMatchRuleOperation
{
public:
	FPCGExMatchRandomConfig Config;

	virtual bool PrepareForMatchableSources(FPCGExContext* InContext, const TSharedPtr<TArray<FPCGExTaggedData>>& InMatchableSources) override;
	virtual bool Test(const PCGExData::FConstPoint& InTargetElement, const FPCGExTaggedData& InCandidate, const PCGExMatching::FScope& InMatchingScope) const override;

protected:
	TArray<TSharedPtr<PCGExData::TAttributeBroadcaster<double>>> ThresholdGetters;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExMatchRandomFactory : public UPCGExMatchRuleFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExMatchRandomConfig Config;

	virtual bool WantsPoints() const override;

	virtual TSharedPtr<FPCGExMatchRuleOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="DataMatch", meta=(PCGExNodeLibraryDoc="utilities/data-matching/match-random"))
class UPCGExCreateMatchRandomSettings : public UPCGExMatchRuleFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(MatchRandom, "Match : Random", "Randomly pass or fail match", FName (GetDisplayName ()))

#endif
	//~End UPCGSettings

	/** Rules properties */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExMatchRandomConfig Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

protected:
	virtual bool IsCacheable() const override
	{
		return true;
	}

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
