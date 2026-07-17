// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Utils/PCGExCompare.h"

#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExProbeNumericCompare.generated.h"

namespace PCGExProbing
{
	struct FCandidate;
}

USTRUCT(BlueprintType)
struct FPCGExProbeConfigNumericCompare : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigNumericCompare() = default;

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

	/** Attribute to compare */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector Attribute;

	/** Comparison check */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayName="Comparison"))
	EPCGExComparison Comparison = EPCGExComparison::StrictlyGreater;

	/** Rounding mode for approx. comparison modes */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, EditCondition="Comparison == EPCGExComparison::NearlyEqual || Comparison == EPCGExComparison::NearlyNotEqual", EditConditionHides))
	double Tolerance = DBL_COMPARE_TOLERANCE;

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
class FPCGExProbeNumericCompare : public FPCGExProbeOperation
{
public:
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigNumericCompare Config;

	TSharedPtr<PCGExDetails::TSettingValue<int32>> MaxConnections;
	TSharedPtr<PCGExData::TBuffer<double>> ValuesBuffer;

protected:
	FVector CWCoincidenceTolerance = FVector::OneVector;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryNumericCompare : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigNumericCompare Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-numeric-compare"))
class UPCGExProbeNumericCompareProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeNumericCompare, "Probe : Numeric Compare", "Connect points that pass the value comparison between the probing point and the candidate point.", FName(GetDisplayName()))
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigNumericCompare Config;


#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
