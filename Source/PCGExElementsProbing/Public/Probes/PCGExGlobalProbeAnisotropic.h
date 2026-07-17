// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeAnisotropic.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigGlobalAnisotropic : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigGlobalAnisotropic()
		: FPCGExProbeConfigBase(true)
	{
	}

	/** Primary axis (preferred connection direction) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FVector PrimaryAxis = FVector::ForwardVector;

	/** Secondary axis (cross direction) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FVector SecondaryAxis = FVector::RightVector;

	/** Scale factor for primary axis (>1 = prefer connections along this axis) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="0.1"))
	double PrimaryScale = 1.0;

	/** Scale factor for secondary axis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="0.1"))
	double SecondaryScale = 2.0;

	/** Scale factor for tertiary axis (computed as cross product) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="0.1"))
	double TertiaryScale = 2.0;

	/** Number of nearest neighbors (in GlobalAnisotropic distance) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 K = 5;

	/** If true, uses per-point normals as primary axis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bUsePerPointNormal = false;
};

class FPCGExProbeGlobalAnisotropic : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool WantsOctree() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigGlobalAnisotropic Config;

protected:
	double ComputeGlobalAnisotropicDistSq(const FVector& Delta, const FMatrix& Transform) const;
	FMatrix BuildTransformMatrix(const FVector& Primary, const FVector& Secondary) const;
};

// Factory classes...
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryGlobalAnisotropic : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigGlobalAnisotropic Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-global-anisotropic"))
class UPCGExProbeGlobalAnisotropicProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeGlobalAnisotropic, "G-Probe : GlobalAnisotropic", "Ellipsoidal distance metric for directional connectivity")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigGlobalAnisotropic Config;
};
