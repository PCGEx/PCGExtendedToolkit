// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExNoise3DFactoryProvider.h"
#include "Core/PCGExNoise3DOperation.h"
#include "UObject/Object.h"

#include "PCGExNoiseOpenSimplex2.generated.h"

namespace PCGExOpenSimplex2
{
	// OpenSimplex2 gradients (24 gradients on edges of a rhombic dodecahedron)
	constexpr double STRETCH_3D = -0.1666666667; // -1.0 / 6.0;
	constexpr double SQUISH_3D = 0.3333333333;   // 1.0 / 3.0;
	constexpr double NORM_3D = 103.0;

	constexpr int8 Gradients3D[] = {
		-11, 4, 4, -4, 11, 4, -4, 4, 11,
		11, 4, 4, 4, 11, 4, 4, 4, 11,
		-11, -4, 4, -4, -11, 4, -4, -4, 11,
		11, -4, 4, 4, -11, 4, 4, -4, 11,
		-11, 4, -4, -4, 11, -4, -4, 4, -11,
		11, 4, -4, 4, 11, -4, 4, 4, -11,
		-11, -4, -4, -4, -11, -4, -4, -4, -11,
		11, -4, -4, 4, -11, -4, 4, -4, -11,
	};

	/** hash byte -> (hash % 24) * 3, precomputed to keep integer modulo out of the per-corner path */
	struct FGradIndexTable
	{
		uint16 V[256] = {};
	};

	constexpr FGradIndexTable MakeGradIndexTable()
	{
		FGradIndexTable Table{};
		for (int32 i = 0; i < 256; ++i)
		{
			Table.V[i] = static_cast<uint16>(i % 24 * 3);
		}
		return Table;
	}

	inline constexpr FGradIndexTable GradIndexTable = MakeGradIndexTable();
}

USTRUCT(BlueprintType)
struct FPCGExNoiseConfigOpenSimplex2 : public FPCGExNoise3DConfigBase
{
	GENERATED_BODY()

	FPCGExNoiseConfigOpenSimplex2()
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
 * OpenSimplex2 noise - patent-free alternative to Simplex
 * Better visual isotropy than classic Simplex
 */
class PCGEXNOISE3D_API FPCGExNoiseOpenSimplex2 : public FPCGExNoise3DOperation
{
public:
	virtual ~FPCGExNoiseOpenSimplex2() override = default;

protected:
	virtual double GenerateRaw(const FVector& Position) const override;

private:
	FORCEINLINE double Contrib(int32 XSV, int32 YSV, int32 ZSV, double DX, double DY, double DZ) const
	{
		double Attn = 2.0 / 3.0 - DX * DX - DY * DY - DZ * DZ;
		if (Attn <= 0)
		{
			return 0;
		}

		const int32 GI = PCGExOpenSimplex2::GradIndexTable.V[PCGExNoise3D::Math::Hash3DSeed(XSV, YSV, ZSV, Seed)];
		const double GX = PCGExOpenSimplex2::Gradients3D[GI];
		const double GY = PCGExOpenSimplex2::Gradients3D[GI + 1];
		const double GZ = PCGExOpenSimplex2::Gradients3D[GI + 2];

		Attn *= Attn;
		return Attn * Attn * (GX * DX + GY * DY + GZ * DZ);
	}
};

////

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Data")
class UPCGExNoise3DFactoryOpenSimplex2 : public UPCGExNoise3DFactoryData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FPCGExNoiseConfigOpenSimplex2 Config;

	virtual TSharedPtr<FPCGExNoise3DOperation> CreateOperationInternal(FPCGExContext* InContext) const override;
	PCGEX_NOISE3D_FACTORY_BOILERPLATE
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural), Category = "PCGEx|Noise", meta=(PCGExNodeLibraryDoc="utilities/noise/noise-opensimplex2"))
class UPCGExNoise3DOpenSimplex2ProviderSettings : public UPCGExNoise3DFactoryProviderSettings
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	PCGEX_NODE_INFOS(Noise3DOpenSimplex2, "Noise : OpenSimplex2", "OpenSimplex2 - patent-free, high quality gradient noise.")
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, ShowOnlyInnerProperties))
	FPCGExNoiseConfigOpenSimplex2 Config;

	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;
};
