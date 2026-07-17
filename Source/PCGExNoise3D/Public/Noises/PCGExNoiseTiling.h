// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseTiling.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigTiling : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigTiling()
		: FPCGExNoise3DConfigBase()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "16"))
	int32 Octaves = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "4.0"))
	double Lacunarity = 2.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Persistence = 0.5;

	/** Tile period on X axis */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "256"))
	int32 PeriodX = 4;

	/** Tile period on Y axis */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "256"))
	int32 PeriodY = 4;

	/** Tile period on Z axis */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "256"))
	int32 PeriodZ = 4;
};

/**
 * Tiling noise - seamlessly tileable gradient noise
 * Good for: tileable textures, seamless backgrounds, repeating patterns
 */
class PCGEXNOISE3D_API FPCGExNoiseTiling : public FPCGExNoise3DOperation
{
public:
	int32 PeriodX = 4;
	int32 PeriodY = 4;
	int32 PeriodZ = 4;

	virtual ~FPCGExNoiseTiling() override = default;

	// Override for special fractal handling with tiling
	virtual double GetDouble(const FVector& Position) const override;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Positive modulo */
	FORCEINLINE int32 Mod(int32 X, int32 P) const
	{
		return ((X % P) + P) % P;
	}
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryTiling : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigTiling Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-tiling"))
class UPCGExNoise3DTilingProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DTiling, "Noise : Tiling", "Tiling noise - seamlessly tileable patterns.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigTiling Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
