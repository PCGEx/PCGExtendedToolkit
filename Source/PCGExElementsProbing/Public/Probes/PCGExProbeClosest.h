// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "UObject/Object.h"

#include "PCGExProbeClosest.generated.h"

namespace PCGExProbing
{
	struct FCandidate;
}

USTRUCT(BlueprintType)
struct FPCGExProbeConfigClosest : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigClosest()
		: FPCGExProbeConfigBase()
	{
	}

	/** Max number of connections. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Max Connections"))
	FPCGExInputShorthandSelectorInteger32Abs MaxConnections = FPCGExInputShorthandSelectorInteger32Abs(FName("@Last"), 1, false);

#pragma region DEPRECATED

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	EPCGExInputValueType MaxConnectionsInput_DEPRECATED = EPCGExInputValueType::Constant;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	FPCGAttributePropertyInputSelector MaxConnectionsAttribute_DEPRECATED;

	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	int32 MaxConnectionsConstant_DEPRECATED = 1;

#pragma endregion

	/** Attempts to prevent connections that are roughly in the same direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, InlineEditConditionToggle))
	bool bPreventCoincidence = true;

	/** Attempts to prevent connections that are roughly in the same direction */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="bPreventCoincidence", ClampMin=0.00001))
	double CoincidencePreventionTolerance = 0.001;

#if WITH_EDITOR
	virtual void ApplyDeprecation() override;
	virtual void RenamePins(const UPCGSettings* InSettings, UPCGNode* InOutNode) const override;
#endif
};

/**
 * 
 */
class FPCGExProbeClosest : public FPCGExProbeOperation
{
public:
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigClosest Config;

	TSharedPtr<PCGExDetails::TSettingValue<int32>> MaxConnections;

protected:
	FVector CWCoincidenceTolerance = FVector::OneVector;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryClosest : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigClosest Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-closest"))
class UPCGExProbeClosestProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeClosest, "Probe : Closests", "Probe in a given Closest.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigClosest Config;


#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
