// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseOpenSimplex2.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;
using namespace PCGExOpenSimplex2;

double FPCGExNoiseOpenSimplex2::GenerateRaw(const FVector& Position) const
{
	// Skew input
	const double S = (Position.X + Position.Y + Position.Z) * SQUISH_3D;
	const double XS = Position.X + S;
	const double YS = Position.Y + S;
	const double ZS = Position.Z + S;

	const int32 XSB = FastFloor(XS);
	const int32 YSB = FastFloor(YS);
	const int32 ZSB = FastFloor(ZS);

	const double XSI = XS - XSB;
	const double YSI = YS - YSB;
	const double ZSI = ZS - ZSB;

	// Unskew
	const double SQ = (XSI + YSI + ZSI) * STRETCH_3D;
	double DX0 = XSI + SQ;
	double DY0 = YSI + SQ;
	double DZ0 = ZSI + SQ;

	double Value = 0.0;

	// Contribution from (0,0,0)
	Value += Contrib(XSB, YSB, ZSB, DX0, DY0, DZ0);

	// Contribution from (1,0,0)
	Value += Contrib(XSB + 1, YSB, ZSB, DX0 - 1 - STRETCH_3D, DY0 - STRETCH_3D, DZ0 - STRETCH_3D);

	// Contribution from (0,1,0)
	Value += Contrib(XSB, YSB + 1, ZSB, DX0 - STRETCH_3D, DY0 - 1 - STRETCH_3D, DZ0 - STRETCH_3D);

	// Contribution from (0,0,1)
	Value += Contrib(XSB, YSB, ZSB + 1, DX0 - STRETCH_3D, DY0 - STRETCH_3D, DZ0 - 1 - STRETCH_3D);

	// Contribution from (1,1,0)
	Value += Contrib(XSB + 1, YSB + 1, ZSB, DX0 - 1 - 2 * STRETCH_3D, DY0 - 1 - 2 * STRETCH_3D, DZ0 - 2 * STRETCH_3D);

	// Contribution from (1,0,1)
	Value += Contrib(XSB + 1, YSB, ZSB + 1, DX0 - 1 - 2 * STRETCH_3D, DY0 - 2 * STRETCH_3D, DZ0 - 1 - 2 * STRETCH_3D);

	// Contribution from (0,1,1)
	Value += Contrib(XSB, YSB + 1, ZSB + 1, DX0 - 2 * STRETCH_3D, DY0 - 1 - 2 * STRETCH_3D, DZ0 - 1 - 2 * STRETCH_3D);

	// Contribution from (1,1,1)
	Value += Contrib(XSB + 1, YSB + 1, ZSB + 1, DX0 - 1 - 3 * STRETCH_3D, DY0 - 1 - 3 * STRETCH_3D, DZ0 - 1 - 3 * STRETCH_3D);

	return Value / NORM_3D * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryOpenSimplex2::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseOpenSimplex2)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(OpenSimplex2, {})

UPCGExFactoryData* UPCGExNoise3DOpenSimplex2ProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryOpenSimplex2* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryOpenSimplex2>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
