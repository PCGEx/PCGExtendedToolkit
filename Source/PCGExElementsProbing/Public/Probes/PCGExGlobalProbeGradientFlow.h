// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "UObject/Object.h"

#include "PCGExGlobalProbeGradientFlow.generated.h"

namespace PCGExProbing
{
	struct FCandidate;
}

namespace PCGExData
{
	template <typename T>
	class TBuffer;
}

UENUM()
enum class EPCGExProbeGradientFlowMode : uint8
{
	Default = 0 UMETA(DisplayName = "Default", ToolTip=""),
	Mutual  = 1 UMETA(DisplayName = "Mutual", ToolTip=""),
};

USTRUCT(BlueprintType)
struct FPCGExProbeConfigGradientFlow : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigGradientFlow()
		: FPCGExProbeConfigBase(true)
	{
		FlowAttribute.Update(TEXT("$Density"));
	}

	// If true, only connect to higher values (flow uphill)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bUphillOnly = false;

	// If true, only connect to the steepest neighbor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bSteepestOnly = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector FlowAttribute;
};

/**
 * 
 */
class FPCGExProbeGradientFlow : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool WantsOctree() const override;

	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigGradientFlow Config;
	TSharedPtr<PCGExData::TBuffer<double>> FlowBuffer;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryGradientFlow : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigGradientFlow Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-gradient-flow"))
class UPCGExProbeGradientFlowProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeGradientFlow, "G-Probe : Gradient Flow", "K-Nearest Neighbors")
#endif
	//~End UPCGSettings

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	/** Filter Config.*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigGradientFlow Config;
};
