// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoisePerlin.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigPerlin : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigPerlin()
		: FPCGExNoise3DConfigBase()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "16"))
	int32 Octaves = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "4.0"))
	double Lacunarity = 2.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Persistence = 0.5;
};

/**
 * Classic Perlin gradient noise
 */
class PCGEXNOISE3D_API FPCGExNoisePerlin : public FPCGExNoise3DOperation
{
public:
	virtual ~FPCGExNoisePerlin() override = default;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryPerlin : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigPerlin Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-perlin"))
class UPCGExNoise3DPerlinProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DPerlin, "Noise : Perlin", "Classic Perlin gradient noise.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigPerlin Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
