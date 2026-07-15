// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExProbeFactoryProvider.h"
#include "Core/PCGExProbeOperation.h"

#include "PCGExGlobalProbeLevelSet.generated.h"

USTRUCT(BlueprintType)
struct FPCGExProbeConfigLevelSet : public FPCGExProbeConfigBase
{
	GENERATED_BODY()

	FPCGExProbeConfigLevelSet()
		: FPCGExProbeConfigBase(true)
	{
		LevelAttribute.Update(TEXT("$Position.Z"));
	}

	/** Attribute defining the scalar field */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	FPCGAttributePropertyInputSelector LevelAttribute;

	/** Max difference in scalar value to allow connection */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="0"))
	double MaxLevelDifference = 10.0;

	/** If true, normalizes level values to 0-1 range before comparison */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable))
	bool bNormalizeLevels = false;

	/** Connect K nearest within level tolerance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PCG_Overridable, ClampMin="1"))
	int32 MaxConnectionsPerPoint = 4;
};

class FPCGExProbeLevelSet : public FPCGExProbeOperation
{
public:
	virtual bool IsGlobalProbe() const override;
	virtual bool WantsOctree() const override;
	virtual bool Prepare(FPCGExContext* InContext) override;
	virtual void ProcessAll(TSet<uint64>& OutEdges) const override;

	FPCGExProbeConfigLevelSet Config;
	TSharedPtr<PCGExData::TBuffer<double>> LevelBuffer;
	double LevelMin = 0, LevelMax = 1;
};

// Factory classes...
UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class UPCGExProbeFactoryLevelSet : public UPCGExProbeFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExProbeConfigLevelSet Config;
	virtual TSharedPtr<FPCGExProbeOperation> CreateOperation(FPCGExContext* InContext) const override;
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Graph|Params", meta=(PCGExNodeLibraryDoc="clusters/generate/cluster-connect-points/g-probe-level-set"))
class UPCGExProbeLevelSetProviderSettings : public UPCGExProbeFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PCGExApplyDeprecationBeforeUpdatePins(UPCGNode* InOutNode, TArray<TObjectPtr<UPCGPin>>& InputPins, TArray<TObjectPtr<UPCGPin>>& OutputPins) override;
	virtual void PCGExApplyDeprecation(UPCGNode* InOutNode) override;
	PCGEX_NODE_INFOS(ProbeLevelSet, "G-Probe : Level Set", "Connects points with similar scalar values (isolines/contours)")
#endif
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExProbeConfigLevelSet Config;
};
