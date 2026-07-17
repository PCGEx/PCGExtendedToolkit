// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseCurl.generated.h"

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigCurl : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigCurl()
		: FPCGExNoise3DConfigBase()
	{
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "1.0", ClampMax = "4.0"))
	double Lacunarity = 2.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Persistence = 0.5;

	/** Epsilon for derivative computation */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0001", ClampMax = "0.1"))
	double Epsilon = 0.001;

	/** Scale the curl magnitude */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.1", ClampMax = "10.0"))
	double CurlScale = 1.0;
};

/**
 * Curl noise - divergence-free noise for fluid simulation
 * Outputs a 3D vector field where ∇·F = 0
 */
class PCGEXNOISE3D_API FPCGExNoiseCurl : public FPCGExNoise3DOperation
{
public:
	double Epsilon = 0.001;
	double CurlScale = 1.0;

	virtual ~FPCGExNoiseCurl() override = default;

	// Override vector outputs for curl field
	virtual double GetDouble(const FVector& Position) const override;
	virtual FVector2D GetVector2D(const FVector& Position) const override;
	virtual FVector GetVector(const FVector& Position) const override;
	virtual FVector4 GetVector4(const FVector& Position) const override;

	virtual void PostInitDerived() override;

private:
	/** Precomputed in PostInitDerived */
	double InvE2 = 500.0;

	FVector GetPotentialField(const FVector& Position) const;
	FVector ComputeCurl(const FVector& Position) const;

	/** Single-component curl paths; must produce the exact same values as the matching ComputeCurl component */
	double ComputeCurlX(const FVector& Position) const;
	double ComputeCurlY(const FVector& Position) const;

	/** Octave accumulation shared by all output arities */
	template <typename ValueType, typename ComputeFn>
	ValueType AccumulateOctaves(const FVector& Position, ComputeFn&& Compute) const;
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryCurl : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigCurl Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-curl"))
class UPCGExNoise3DCurlProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DCurl, "Noise : Curl", "Curl noise - divergence-free for fluids and particles.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigCurl Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
