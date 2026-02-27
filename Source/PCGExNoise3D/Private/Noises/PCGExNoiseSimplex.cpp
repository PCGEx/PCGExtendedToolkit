// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseSimplex.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseSimplex::GenerateRaw(const FVector& Position) const
{
	// Skew input space to determine which simplex cell we're in
	const double S = (Position.X + Position.Y + Position.Z) * F3;
	const int32 I = FastFloor(Position.X + S);
	const int32 J = FastFloor(Position.Y + S);
	const int32 K = FastFloor(Position.Z + S);

	// Unskew cell origin back to (x,y,z) space
	const double T = (I + J + K) * G3;
	const double X0 = Position.X - (I - T);
	const double Y0 = Position.Y - (J - T);
	const double Z0 = Position.Z - (K - T);

	// Determine which simplex we're in
	int32 I1, J1, K1; // Offsets for second corner
	int32 I2, J2, K2; // Offsets for third corner

	if (X0 >= Y0)
	{
		if (Y0 >= Z0)
		{
			I1 = 1;
			J1 = 0;
			K1 = 0;
			I2 = 1;
			J2 = 1;
			K2 = 0;
		}
		else if (X0 >= Z0)
		{
			I1 = 1;
			J1 = 0;
			K1 = 0;
			I2 = 1;
			J2 = 0;
			K2 = 1;
		}
		else
		{
			I1 = 0;
			J1 = 0;
			K1 = 1;
			I2 = 1;
			J2 = 0;
			K2 = 1;
		}
	}
	else
	{
		if (Y0 < Z0)
		{
			I1 = 0;
			J1 = 0;
			K1 = 1;
			I2 = 0;
			J2 = 1;
			K2 = 1;
		}
		else if (X0 < Z0)
		{
			I1 = 0;
			J1 = 1;
			K1 = 0;
			I2 = 0;
			J2 = 1;
			K2 = 1;
		}
		else
		{
			I1 = 0;
			J1 = 1;
			K1 = 0;
			I2 = 1;
			J2 = 1;
			K2 = 0;
		}
	}

	// Offsets for remaining corners
	const double X1 = X0 - I1 + G3;
	const double Y1 = Y0 - J1 + G3;
	const double Z1 = Z0 - K1 + G3;

	const double X2 = X0 - I2 + 2.0 * G3;
	const double Y2 = Y0 - J2 + 2.0 * G3;
	const double Z2 = Z0 - K2 + 2.0 * G3;

	const double X3 = X0 - 1.0 + 3.0 * G3;
	const double Y3 = Y0 - 1.0 + 3.0 * G3;
	const double Z3 = Z0 - 1.0 + 3.0 * G3;

	// Hash coordinates of corners
	const int32 II = (I + Seed) & 255;
	const int32 JJ = J & 255;
	const int32 KK = K & 255;

	const int32 GI0 = Hash3D(II, JJ, KK);
	const int32 GI1 = Hash3D(II + I1, JJ + J1, KK + K1);
	const int32 GI2 = Hash3D(II + I2, JJ + J2, KK + K2);
	const int32 GI3 = Hash3D(II + 1, JJ + 1, KK + 1);

	// Calculate contributions from corners
	const double N0 = Contrib(GI0, X0, Y0, Z0);
	const double N1 = Contrib(GI1, X1, Y1, Z1);
	const double N2 = Contrib(GI2, X2, Y2, Z2);
	const double N3 = Contrib(GI3, X3, Y3, Z3);

	// Sum contributions and scale to [0, 1]
	return 32.0 * (N0 + N1 + N2 + N3) * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactorySimplex::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseSimplex)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Simplex, {})

UPCGExFactoryData* UPCGExNoise3DSimplexProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactorySimplex* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactorySimplex>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
