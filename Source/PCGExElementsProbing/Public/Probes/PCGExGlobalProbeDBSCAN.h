// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeDBSCAN.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigDBSCAN : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigDBSCAN()
		: FPCGExProbeConfigBase(true)
	{
	}

	/** Minimum points within Epsilon to be considered a core point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MinPoints = 3;

	/** If true, only connects core points to each other */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bCoreToCorOnly = true;

	/** If true, connects border points to their nearest core point only. If false, connects to all reachable core points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bBorderToNearestCoreOnly = true;
};

class FPCGExProbeDBSCAN : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool WantsOctree() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigDBSCAN Config;
};

// Factory classes...
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryDBSCAN : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigDBSCAN Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-dbscan"))
class UPCGExProbeDBSCANProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeDBSCAN, "G-Probe : DBSCAN", "Density-based connectivity/reachability (DBSCAN-style)")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigDBSCAN Config;
};
