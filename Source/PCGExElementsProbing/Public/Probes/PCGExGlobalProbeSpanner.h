// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeSpanner.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigSpanner : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigSpanner()
		: FPCGExProbeConfigBase(false)
	{
	}

	/** Stretch factor - path through graph is at most t * Euclidean distance. Lower = denser graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="1.0", ClampMax="10.0"))
	double StretchFactor = 2.0;

	/** Max edges to consider (performance limit) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="100"))
	int32 MaxEdgeCandidates = 50000;
};

class FPCGExProbeSpanner : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigSpanner Config;

protected:
	// Dijkstra helper - returns shortest path distance between two nodes in current graph
	double GetGraphDistance(int32 From, int32 To, const TArray<TSet<int32>>& Adjacency,
	                        const TArray<FVector>& Positions) const;
};

// Factory classes...
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactorySpanner : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigSpanner Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-greedy-spanner"))
class UPCGExProbeSpannerProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeSpanner, "G-Probe : Greedy Spanner", "Greedy t-spanner - sparse graph with path length guarantees")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigSpanner Config;
};
