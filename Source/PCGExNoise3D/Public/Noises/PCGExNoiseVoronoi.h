// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseVoronoi.generated.h"

UENUM(BlueprintType)
enum class EPCGExVoronoiOutput : uint8
{
	CellValue UMETA(DisplayName = "Cell Value"),
	Distance UMETA(DisplayName = "Distance to Center"),
	EdgeDistance UMETA(DisplayName = "Edge Distance"),
	Crackle UMETA(DisplayName = "Crackle (F2-F1)")
};

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigVoronoi : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigVoronoi()
		: FPCGExNoise3DConfigBase()
	{
	}

	/** Output type */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExVoronoiOutput OutputType = EPCGExVoronoiOutput::CellValue;

	/** Jitter amount */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Jitter = 1.0;

	/** Smoothness for smooth distance mode */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Smoothness = 0.0;
};

/**
 * Voronoi noise - cell-based patterns with multiple output modes
 */
class PCGEXNOISE3D_API FPCGExNoiseVoronoi : public FPCGExNoise3DOperation
{
public:
	EPCGExVoronoiOutput OutputMode = EPCGExVoronoiOutput::CellValue;
	double Jitter = 1.0;
	double Smoothness = 0.0;

	virtual ~FPCGExNoiseVoronoi() override = default;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Smooth minimum for blending cells */
	FORCEINLINE double SmoothMin(double A, double B, double K) const
	{
		if (K <= 0.0)
		{
			return FMath::Min(A, B);
		}
		const double H = FMath::Max(K - FMath::Abs(A - B), 0.0) / K;
		return FMath::Min(A, B) - H * H * K * 0.25;
	}
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryVoronoi : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigVoronoi Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-voronoi"))
class UPCGExNoise3DVoronoiProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DVoronoi, "Noise : Voronoi", "Voronoi noise - cell patterns with multiple modes.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigVoronoi Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
