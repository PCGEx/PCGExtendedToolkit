// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseGabor.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigGabor : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigGabor()
		: FPCGExNoise3DConfigBase()
	{
	}

	/** Direction of the gabor kernel (will be normalized) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FVector Direction = FVector(1, 0, 0);

	/** Bandwidth - controls how directional (lower = more directional) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.1", ClampMax = "10.0"))
	double Bandwidth = 1.0;

	/** Number of impulses per cell */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "32"))
	int32 ImpulsesPerCell = 8;

	/** Kernel radius */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.5", ClampMax = "4.0"))
	double KernelRadius = 1.5;
};

/**
 * Gabor noise - anisotropic noise with controllable direction
 * Good for: wood grain, fabric, brushed metal, directional patterns
 */
class PCGEXNOISE3D_API FPCGExNoiseGabor : public FPCGExNoise3DOperation
{
public:
	FVector Direction = FVector(1, 0, 0);
	double Bandwidth = 1.0;
	int32 ImpulsesPerCell = 8;
	double KernelRadius = 1.5;

	virtual ~FPCGExNoiseGabor() override = default;

	virtual void PostInitDerived() override;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Precomputed in PostInitDerived */
	int32 SearchRadius = 2;
	double KernelRadiusSq = 2.25;
	double Normalization = 1.0;
	double GaussCoeff = -PI;
	double PhaseCoeff = 2.0 * PI;

	FORCEINLINE double GaborKernel(const FVector& Offset) const
	{
		const double R2 = Offset.SizeSquared();
		if (R2 > KernelRadiusSq)
		{
			return 0.0;
		}

		// Gaussian envelope
		const double Gaussian = FMath::Exp(GaussCoeff * R2);

		// Sinusoidal carrier
		const double Phase = PhaseCoeff * FVector::DotProduct(Direction, Offset);

		return Gaussian * FMath::Cos(Phase);
	}

	/** Offset within the cell + signed weight, both derived from a single hash */
	FORCEINLINE FVector RandomImpulse(int32 CellX, int32 CellY, int32 CellZ, int32 Idx, double& OutWeight) const
	{
		const uint32 H = PCGExNoise3D::Math::Hash32(CellX + Seed, CellY + Idx, CellZ);
		OutWeight = PCGExNoise3D::Math::Hash32ToDouble01(PCGExNoise3D::Math::Hash32Remix(H)) * 2.0 - 1.0;
		return PCGExNoise3D::Math::Hash32ToVector01(H);
	}
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryGabor : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigGabor Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-gabor"))
class UPCGExNoise3DGaborProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DGabor, "Noise : Gabor", "Gabor noise - directional/anisotropic patterns.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigGabor Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
