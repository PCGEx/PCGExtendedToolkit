// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseMarble.generated.h"

UENUM(BlueprintType)
enum class EPCGExMarbleDirection : uint8
{
	X UMETA(DisplayName = "X Axis"),
	Y UMETA(DisplayName = "Y Axis"),
	Z UMETA(DisplayName = "Z Axis"),
	Radial UMETA(DisplayName = "Radial")
};

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigMarble : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigMarble()
		: FPCGExNoise3DConfigBase()
	{
	}

	/** Direction of marble veins */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExMarbleDirection Direction = EPCGExMarbleDirection::X;

	/** Frequency of the sine wave creating veins */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.1", ClampMax = "20.0"))
	double VeinFrequency = 5.0;

	/** Strength of turbulence distortion */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "5.0"))
	double TurbulenceStrength = 1.0;

	/** Number of turbulence octaves */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "8"))
	int32 TurbulenceOctaves = 4;

	/** Sharpness of vein edges (1 = soft, higher = sharper) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "8.0"))
	double VeinSharpness = 1.0;
};

/**
 * Marble noise - sine-based vein patterns with turbulence distortion
 * Good for: marble textures, wood grain, geological patterns, stylized effects
 */
class PCGEXNOISE3D_API FPCGExNoiseMarble : public FPCGExNoise3DOperation
{
public:
	EPCGExMarbleDirection Direction = EPCGExMarbleDirection::X;
	double VeinFrequency = 5.0;
	double TurbulenceStrength = 1.0;
	int32 TurbulenceOctaves = 4;
	double VeinSharpness = 1.0;

	virtual ~FPCGExNoiseMarble() override = default;

	virtual void PostInitDerived() override;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Precomputed in PostInitDerived */
	double InvTurbulenceNorm = 1.0;

	double GenerateTurbulence(const FVector& Position) const;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryMarble : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigMarble Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-marble"))
class UPCGExNoise3DMarbleProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DMarble, "Noise : Marble", "Marble noise - veined patterns with turbulence.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigMarble Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
