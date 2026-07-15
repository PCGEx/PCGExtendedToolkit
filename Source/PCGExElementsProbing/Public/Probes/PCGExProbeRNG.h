// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"
#include "UObject/Object.h"

#include "PCGExProbeRNG.generated.h"

namespace PCGExProbing
{
	struct FCandidate;
}

USTRUCT(BlueprintType)
struct FPCGExProbeConfigRNG : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigRNG()
		: FPCGExProbeConfigBase()
	{
	}

	/**
	 * β-skeleton parameter controlling the forbidden lune shape.
	 * β = 1 → Gabriel Graph (diametric circle test; most edges)
	 * β = 2 → Relative Neighborhood Graph (lune test; fewest edges)
	 * Intermediate values interpolate between the two.
	 * Graph density: MST ⊂ RNG ⊂ Gabriel ⊂ Delaunay.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ClampMin="1.0", ClampMax="2.0"))
	double Beta = 2.0;
};

class FPCGExProbeRNG : public FPCGExProbeOperation
{
public:
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessCandidates(const int32 Index, TArray<PCGExProbing::FCandidate>& Candidates, TSet<uint64>* Coincidence, const FVector& ST, TSet<uint64>* OutEdges, PCGExMT::FScopedContainer* Container) override;

	FPCGExProbeConfigRNG Config;

protected:
	double HalfBeta = 1.0;
	double HalfBetaSq = 1.0;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryRNG : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigRNG Config;

	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/probe-rng"))
class UPCGExProbeRNGProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS_CUSTOM_SUBTITLE(ProbeRNG, "Probe : RNG", "β-skeleton probe. β=2 → Relative Neighborhood Graph (lune test). β=1 → Gabriel Graph (diametric circle test).", FName(GetDisplayName()))
#endif

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigRNG Config;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif
};
