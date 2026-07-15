// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeChain.generated.h"

UENUM()
enum class EPCGExProbeChainSortMode : uint8
{
	ByAttribute      = 0 UMETA(DisplayName = "By Attribute", ToolTip="Sort by a scalar attribute"),
	ByAxisProjection = 1 UMETA(DisplayName = "By Axis Projection", ToolTip="Sort by projection onto an axis"),
	BySpatialCurve   = 2 UMETA(DisplayName = "By Spatial Curve (TSP)", ToolTip="Greedy traveling salesman approximation"),
	ByHilbertCurve   = 3 UMETA(DisplayName = "By Hilbert Curve", ToolTip="Sort by Hilbert curve index for spatial locality"),
};

USTRUCT(BlueprintType)
struct FPCGExProbeConfigChain : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigChain()
		: FPCGExProbeConfigBase(true)
	{
		SortAttribute.Update(TEXT("$Density"));
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	EPCGExProbeChainSortMode SortMode = EPCGExProbeChainSortMode::ByAttribute;

	/** Attribute to sort by (for ByAttribute mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, EditCondition="SortMode == EPCGExProbeChainSortMode::ByAttribute"))
	FPCGAttributePropertyInputSelector SortAttribute;

	/** Axis to project onto (for ByAxisProjection mode) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, EditCondition="SortMode == EPCGExProbeChainSortMode::ByAxisProjection"))
	FVector ProjectionAxis = FVector::ForwardVector;

	/** If true, creates a closed loop connecting last to first */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bClosedLoop = false;
};

class FPCGExProbeChain : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigChain Config;
	TSharedPtr<PCGExData::TBuffer<double>> SortBuffer;

protected:
	void ComputeHilbertOrder(TArray<int32>& OutOrder) const;
	void ComputeGreedyTSPOrder(TArray<int32>& OutOrder) const;
};

// Factory classes...
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryChain : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigChain Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-chain-path"))
class UPCGExProbeChainProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeChain, "G-Probe : Chain/Path", "Creates sequential chain connections based on sorting criteria")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigChain Config;
};
