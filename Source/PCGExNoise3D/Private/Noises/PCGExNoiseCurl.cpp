// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseCurl.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

namespace PCGExNoiseCurl
{
	// Noise-space offsets decorrelating the three potential-field channels
	const FVector OffsetFy = FVector(31.416, 47.853, 12.793);
	const FVector OffsetFz = FVector(93.139, 25.186, 71.524);
}

void FPCGExNoiseCurl::PostInitDerived()
{
	InvE2 = 1.0 / (2.0 * Epsilon);
}

FVector FPCGExNoiseCurl::GetPotentialField(const FVector& Position) const
{
	// Three independent noise channels for potential field
	const FVector ScaledPos = Position * Frequency;
	const double Fx = Perlin3D(ScaledPos, Seed);
	const double Fy = Perlin3D(ScaledPos + PCGExNoiseCurl::OffsetFy, Seed);
	const double Fz = Perlin3D(ScaledPos + PCGExNoiseCurl::OffsetFz, Seed);
	return FVector(Fx, Fy, Fz);
}

FVector FPCGExNoiseCurl::ComputeCurl(const FVector& Position) const
{
	const double E = Epsilon;

	// Central differences for partial derivatives
	const FVector PxP = GetPotentialField(Position + FVector(E, 0, 0));
	const FVector PxN = GetPotentialField(Position - FVector(E, 0, 0));
	const FVector PyP = GetPotentialField(Position + FVector(0, E, 0));
	const FVector PyN = GetPotentialField(Position - FVector(0, E, 0));
	const FVector PzP = GetPotentialField(Position + FVector(0, 0, E));
	const FVector PzN = GetPotentialField(Position - FVector(0, 0, E));

	// Partial derivatives
	const double dFz_dy = (PyP.Z - PyN.Z) * InvE2;
	const double dFy_dz = (PzP.Y - PzN.Y) * InvE2;
	const double dFx_dz = (PzP.X - PzN.X) * InvE2;
	const double dFz_dx = (PxP.Z - PxN.Z) * InvE2;
	const double dFy_dx = (PxP.Y - PxN.Y) * InvE2;
	const double dFx_dy = (PyP.X - PyN.X) * InvE2;

	// Curl: (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
	return FVector(
		dFz_dy - dFy_dz,
		dFx_dz - dFz_dx,
		dFy_dx - dFx_dy
		) * CurlScale;
}

double FPCGExNoiseCurl::ComputeCurlX(const FVector& Position) const
{
	// X component only (dFz/dy - dFy/dz)
	const double E = Epsilon;

	const double FzYP = Perlin3D((Position + FVector(0, E, 0)) * Frequency + PCGExNoiseCurl::OffsetFz, Seed);
	const double FzYN = Perlin3D((Position - FVector(0, E, 0)) * Frequency + PCGExNoiseCurl::OffsetFz, Seed);
	const double FyZP = Perlin3D((Position + FVector(0, 0, E)) * Frequency + PCGExNoiseCurl::OffsetFy, Seed);
	const double FyZN = Perlin3D((Position - FVector(0, 0, E)) * Frequency + PCGExNoiseCurl::OffsetFy, Seed);

	const double dFz_dy = (FzYP - FzYN) * InvE2;
	const double dFy_dz = (FyZP - FyZN) * InvE2;

	return (dFz_dy - dFy_dz) * CurlScale;
}

double FPCGExNoiseCurl::ComputeCurlY(const FVector& Position) const
{
	// Y component only (dFx/dz - dFz/dx)
	const double E = Epsilon;

	const double FxZP = Perlin3D((Position + FVector(0, 0, E)) * Frequency, Seed);
	const double FxZN = Perlin3D((Position - FVector(0, 0, E)) * Frequency, Seed);
	const double FzXP = Perlin3D((Position + FVector(E, 0, 0)) * Frequency + PCGExNoiseCurl::OffsetFz, Seed);
	const double FzXN = Perlin3D((Position - FVector(E, 0, 0)) * Frequency + PCGExNoiseCurl::OffsetFz, Seed);

	const double dFx_dz = (FxZP - FxZN) * InvE2;
	const double dFz_dx = (FzXP - FzXN) * InvE2;

	return (dFx_dz - dFz_dx) * CurlScale;
}

template <typename ValueType, typename ComputeFn>
ValueType FPCGExNoiseCurl::AccumulateOctaves(const FVector& Position, ComputeFn&& Compute) const
{
	// Load-bearing asymmetry shared by all output arities: octave 0 samples the
	// transformed position, higher octaves the raw one (kept for compatibility)
	ValueType Acc = Compute(TransformPosition(Position));

	if (Octaves > 1)
	{
		double Amp = 1.0;
		double Freq = 1.0;

		for (int32 i = 1; i < Octaves; ++i)
		{
			Amp *= Persistence;
			Freq *= Lacunarity;
			Acc += Compute(Position * Freq) * Amp;
		}

		Acc *= FractalBounding;
	}

	return Acc;
}

double FPCGExNoiseCurl::GetDouble(const FVector& Position) const
{
	const double CurlX = AccumulateOctaves<double>(Position, [this](const FVector& P) { return ComputeCurlX(P); });
	return ApplyRemap(CurlX * 0.5 + 0.5);
}

FVector2D FPCGExNoiseCurl::GetVector2D(const FVector& Position) const
{
	const FVector2D Curl = AccumulateOctaves<FVector2D>(Position, [this](const FVector& P) { return FVector2D(ComputeCurlX(P), ComputeCurlY(P)); });
	return FVector2D(
		ApplyRemap(Curl.X * 0.5 + 0.5),
		ApplyRemap(Curl.Y * 0.5 + 0.5)
		);
}

FVector FPCGExNoiseCurl::GetVector(const FVector& Position) const
{
	FVector Curl = AccumulateOctaves<FVector>(Position, [this](const FVector& P) { return ComputeCurl(P); });

	for (int i = 0; i < 3; i++)
	{
		Curl[i] = ApplyRemap(Curl[i] * 0.5 + 0.5);
	}

	return Curl;
}

FVector4 FPCGExNoiseCurl::GetVector4(const FVector& Position) const
{
	const FVector Curl = GetVector(Position);
	return FVector4(Curl.X, Curl.Y, Curl.Z, Curl.Size());
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryCurl::CreateOperationInternal(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseCurl)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;
	NewOperation->Epsilon = Config.Epsilon;
	NewOperation->CurlScale = Config.CurlScale;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Curl, {})

UPCGExFactoryData* UPCGExNoise3DCurlProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryCurl* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryCurl>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
