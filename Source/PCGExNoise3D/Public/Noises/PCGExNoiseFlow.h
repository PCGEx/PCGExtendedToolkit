// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseFlow.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigFlow : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigFlow()
		: FPCGExNoise3DConfigBase()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "16"))
	int32 Octaves = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "4.0"))
	double Lacunarity = 2.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Persistence = 0.5;

	/** Time parameter for animation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	double Time = 0.0;

	/** Rotation speed of gradients */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "10.0"))
	double RotationSpeed = 1.0;
};

/**
 * Flow noise - time-coherent animated noise
 * Gradients rotate smoothly over time instead of changing randomly
 * Good for: animated clouds, flowing water, smoke, fire
 */
class PCGEXNOISE3D_API FPCGExNoiseFlow : public FPCGExNoise3DOperation
{
public:
	double Time = 0.0;
	double RotationSpeed = 1.0;

	virtual ~FPCGExNoiseFlow() override = default;

	virtual void PostInitDerived() override;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Time-rotated gradient per hash byte; Time/RotationSpeed are constant per operation so this is exhaustive */
	FVector RotatedGradients[256];
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryFlow : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigFlow Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-flow"))
class UPCGExNoise3DFlowProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DFlow, "Noise : Flow", "Flow noise - time-coherent animated patterns.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigFlow Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
