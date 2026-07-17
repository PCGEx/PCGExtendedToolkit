// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseCaustic.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigCaustic : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigCaustic()
		: FPCGExNoise3DConfigBase()
	{
	}

	/** Number of overlapping wave layers */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "8"))
	int32 WaveLayers = 3;

	/** Base wavelength */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.1", ClampMax = "10.0"))
	double Wavelength = 1.0;

	/** Time parameter for animation (0 for static) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0"))
	double Time = 0.0;

	/** Animation speed */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "5.0"))
	double AnimationSpeed = 1.0;

	/** Caustic intensity (creates bright focal points) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.5", ClampMax = "4.0"))
	double Intensity = 2.0;

	/** Focus sharpness (higher = brighter focal points) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "8.0"))
	double Focus = 2.0;
};

/**
 * Caustic noise - simulates light patterns through water
 * Good for: underwater effects, magical effects, sci-fi patterns, water surfaces
 */
class PCGEXNOISE3D_API FPCGExNoiseCaustic : public FPCGExNoise3DOperation
{
public:
	int32 WaveLayers = 3;
	double Wavelength = 1.0;
	double Time = 0.0;
	double AnimationSpeed = 1.0;
	double Intensity = 2.0;
	double Focus = 2.0;

	virtual ~FPCGExNoiseCaustic() override = default;

	virtual void PostInitDerived() override;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	double GenerateWaveLayer(const FVector& Position, int32 LayerIndex) const;

	/** Per-layer constants precomputed in PostInitDerived */
	struct FWaveLayer
	{
		double CosA = 1.0;
		double SinA = 0.0;
		double TimeOffset = 0.0;
	};

	TArray<FWaveLayer> Layers;
	double InvWavelength = 1.0;
	double InvWaveLayers = 1.0;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryCaustic : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigCaustic Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-caustic"))
class UPCGExNoise3DCausticProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DCaustic, "Noise : Caustic", "Caustic noise - water light patterns.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigCaustic Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
