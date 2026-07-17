// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseWorley.generated.h"

UENUM(BlueprintType)
enum class EPCGExWorleyDistanceFunc : uint8
{
	Euclidean UMETA(DisplayName = "Euclidean"),
	EuclideanSq UMETA(DisplayName = "Euclidean Squared"),
	Manhattan UMETA(DisplayName = "Manhattan"),
	Chebyshev UMETA(DisplayName = "Chebyshev")
};

UENUM(BlueprintType)
enum class EPCGExWorleyReturnType : uint8
{
	F1 UMETA(DisplayName = "F1 (Closest)"),
	F2 UMETA(DisplayName = "F2 (Second Closest)"),
	F2MinusF1 UMETA(DisplayName = "F2 - F1 (Edge Detection)"),
	F1PlusF2 UMETA(DisplayName = "F1 + F2"),
	F1TimesF2 UMETA(DisplayName = "F1 * F2"),
	CellValue UMETA(DisplayName = "Cell Value")
};

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigWorley : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigWorley()
		: FPCGExNoise3DConfigBase()
	{
	}

	/** Distance function to use */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExWorleyDistanceFunc DistanceFunction = EPCGExWorleyDistanceFunc::Euclidean;

	/** What to return */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExWorleyReturnType ReturnType = EPCGExWorleyReturnType::F1;

	/** Jitter amount (0 = regular grid, 1 = maximum randomness) */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ClampMin = "0.0", ClampMax = "1.0"))
	double Jitter = 1.0;
};

/**
 * Worley/Cellular noise
 * Creates cell-like patterns based on distance to feature points
 */
class PCGEXNOISE3D_API FPCGExNoiseWorley : public FPCGExNoise3DOperation
{
public:
	EPCGExWorleyDistanceFunc DistanceFunction = EPCGExWorleyDistanceFunc::Euclidean;
	EPCGExWorleyReturnType ReturnType = EPCGExWorleyReturnType::F1;
	double Jitter = 1.0;

	virtual ~FPCGExNoiseWorley() override = default;

	virtual void PostInitDerived() override;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	/** Approximate normalization for the configured distance function, precomputed in PostInitDerived */
	double MaxDist = 1.0;

	FORCEINLINE double CalcDistance(const FVector& A, const FVector& B) const
	{
		switch (DistanceFunction)
		{
		case EPCGExWorleyDistanceFunc::Euclidean:
			return PCGExNoise3D::Math::DistanceEuclidean(A, B);
		case EPCGExWorleyDistanceFunc::EuclideanSq:
			return PCGExNoise3D::Math::DistanceEuclideanSq(A, B);
		case EPCGExWorleyDistanceFunc::Manhattan:
			return PCGExNoise3D::Math::DistanceManhattan(A, B);
		case EPCGExWorleyDistanceFunc::Chebyshev:
			return PCGExNoise3D::Math::DistanceChebyshev(A, B);
		default:
			return PCGExNoise3D::Math::DistanceEuclidean(A, B);
		}
	}
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryWorley : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigWorley Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-worley"))
class UPCGExNoise3DWorleyProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DWorley, "Noise : Worley", "Worley/Cellular noise - cell-like patterns.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigWorley Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
