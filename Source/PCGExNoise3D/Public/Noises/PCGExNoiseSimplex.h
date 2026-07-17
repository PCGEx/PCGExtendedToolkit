// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseSimplex.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigSimplex : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigSimplex()
		: FPCGExNoise3DConfigBase()
	{
	}

	/** Number of fractal octaves */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "16"))
	int32 Octaves = 1;

	/** Frequency multiplier per octave */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "4.0"))
	double Lacunarity = 2.0;

	/** Amplitude multiplier per octave */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Persistence = 0.5;
};

/**
 * Simplex noise operation
 * Ken Perlin's improved noise algorithm with lower computational cost
 */
class PCGEXNOISE3D_API FPCGExNoiseSimplex : public FPCGExNoise3DOperation
{
public:
	virtual ~FPCGExNoiseSimplex() override = default;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Contribution from a simplex corner */
	FORCEINLINE double Contrib(int32 Hash, double X, double Y, double Z) const
	{
		const double T = 0.6 - X * X - Y * Y - Z * Z;
		if (T < 0.0)
		{
			return 0.0;
		}
		const double T2 = T * T;
		return T2 * T2 * PCGExNoise3D::Math::GradDot3(Hash, X, Y, Z);
	}
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactorySimplex : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigSimplex Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-simplex"))
class UPCGExNoise3DSimplexProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DSimplex, "Noise : Simplex", "Simplex gradient noise - efficient, high quality.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigSimplex Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
