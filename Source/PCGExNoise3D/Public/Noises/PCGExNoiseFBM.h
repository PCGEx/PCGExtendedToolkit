// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseFBM.generated.h"

UENUM(BlueprintType)
enum class EPCGExFBMVariant : uint8
{
	Standard UMETA(DisplayName = "Standard fBm"),
	Ridged UMETA(DisplayName = "Ridged Multifractal"),
	Billow UMETA(DisplayName = "Billow (Turbulence)"),
	Hybrid UMETA(DisplayName = "Hybrid Multifractal"),
	Warped UMETA(DisplayName = "Domain Warped")
};

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigFBM : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigFBM()
		: FPCGExNoise3DConfigBase()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "16"))
	int32 Octaves = 4;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "4.0"))
	double Lacunarity = 2.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Persistence = 0.5;

	/** FBM variant */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExFBMVariant Variant = EPCGExFBMVariant::Standard;

	/** Ridge offset for ridged variant */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "2.0", EditCondition = "Variant == EPCGExFBMVariant::Ridged"))
	double RidgeOffset = 1.0;

	/** Warp strength for warped variant */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "2.0", EditCondition = "Variant == EPCGExFBMVariant::Warped"))
	double WarpStrength = 0.5;
};

/**
 * Fractal Brownian Motion noise with multiple variants
 */
class PCGEXNOISE3D_API FPCGExNoiseFBM : public FPCGExNoise3DOperation
{
public:
	EPCGExFBMVariant Variant = EPCGExFBMVariant::Standard;
	double RidgeOffset = 1.0;
	double WarpStrength = 0.5;

	virtual ~FPCGExNoiseFBM() override = default;

	// Override GetDouble to use custom fractal implementation
	virtual double GetDouble(const FVector& Position) const override;

private:
	double GenerateStandard(const FVector& Position) const;
	double GenerateRidged(const FVector& Position) const;
	double GenerateBillow(const FVector& Position) const;
	double GenerateHybrid(const FVector& Position) const;
	double GenerateWarped(const FVector& Position) const;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryFBM : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigFBM Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-fbm"))
class UPCGExNoise3DFBMProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DFBM, "Noise : FBM", "Fractal Brownian Motion with variants (ridged, billow, warped).")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigFBM Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
