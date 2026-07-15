// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeTheta.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigTheta : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigTheta()
		: FPCGExProbeConfigBase(true)
	{
	}

	/** Number of cones (typically 6-8). Higher = denser graph, better spanner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="4", ClampMax="32"))
	int32 NumCones = 6;

	/** Axis to build cones around (cones are perpendicular to this) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FVector ConeAxis = FVector::UpVector;

	/** If true, uses Yao graph construction (nearest in cone) instead of Theta (projected nearest) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bUseYaoVariant = false;
};

class FPCGExProbeTheta : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool WantsOctree() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigTheta Config;

protected:
	TArray<FVector> ConeBisectors; // Precomputed cone center directions
	float ConeHalfAngle = 0;
};


UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryTheta : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigTheta Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-theta-graph"))
class UPCGExProbeThetaProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeTheta, "G-Probe : Theta Graph", "Theta/Yao graph spanner - connects to nearest in angular cones")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigTheta Config;
};
