// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseFBM.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseFBM::GenerateStandard(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;

	for (int32 i = 0; i < Octaves; ++i)
	{
		Sum += Perlin3D(Position * Freq, Seed) * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * FractalBounding;
}

double FPCGExNoiseFBM::GenerateRidged(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	double Weight = 1.0;

	for (int32 i = 0; i < Octaves; ++i)
	{
		double Noise = Perlin3D(Position * Freq, Seed);
		Noise = RidgeOffset - FMath::Abs(Noise);
		Noise = Noise * Noise;
		Noise *= Weight;
		Weight = FMath::Clamp(Noise * 2.0, 0.0, 1.0);

		Sum += Noise * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * 1.25 - 1.0;
}

double FPCGExNoiseFBM::GenerateBillow(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;

	for (int32 i = 0; i < Octaves; ++i)
	{
		double Noise = Perlin3D(Position * Freq, Seed);
		Noise = FMath::Abs(Noise) * 2.0 - 1.0;
		Sum += Noise * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * FractalBounding;
}

double FPCGExNoiseFBM::GenerateHybrid(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	double Weight = 1.0;

	double Noise = (Perlin3D(Position * Freq, Seed) + RidgeOffset) * Amp;
	Sum = Noise;
	Weight = Noise;
	Amp *= Persistence;
	Freq *= Lacunarity;

	for (int32 i = 1; i < Octaves; ++i)
	{
		Weight = FMath::Clamp(Weight, 0.0, 1.0);
		Noise = (Perlin3D(Position * Freq, Seed) + RidgeOffset) * Amp * Weight;
		Sum += Noise;
		Weight *= 2.0 * Noise;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * 0.5 - 1.0;
}

double FPCGExNoiseFBM::GenerateWarped(const FVector& Position) const
{
	const double WarpFreq = Frequency;

	// First warp layer
	const FVector Warp1(
		Perlin3D(Position * WarpFreq, Seed),
		Perlin3D((Position + FVector(5.2, 1.3, 2.8)) * WarpFreq, Seed),
		Perlin3D((Position + FVector(1.7, 9.2, 3.1)) * WarpFreq, Seed)
		);

	const FVector WarpedPos = Position + Warp1 * WarpStrength;

	// Second warp layer
	const FVector Warp2(
		Perlin3D((WarpedPos + FVector(1.7, 9.2, 3.1)) * WarpFreq, Seed),
		Perlin3D((WarpedPos + FVector(8.3, 2.8, 4.7)) * WarpFreq, Seed),
		Perlin3D((WarpedPos + FVector(2.1, 6.4, 1.8)) * WarpFreq, Seed)
		);

	const FVector FinalPos = WarpedPos + Warp2 * WarpStrength;

	// Standard fBm on warped position
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;

	for (int32 i = 0; i < Octaves; ++i)
	{
		Sum += Perlin3D(FinalPos * Freq, Seed) * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * FractalBounding;
}

double FPCGExNoiseFBM::GetDouble(const FVector& Position) const
{
	double Value;

	switch (Variant)
	{
	case EPCGExFBMVariant::Standard:
		Value = GenerateStandard(TransformPosition(Position)) * 0.5 + 0.5;
		break;
	case EPCGExFBMVariant::Ridged:
		Value = GenerateRidged(TransformPosition(Position)) * 0.5 + 0.5;
		break;
	case EPCGExFBMVariant::Billow:
		Value = GenerateBillow(TransformPosition(Position)) * 0.5 + 0.5;
		break;
	case EPCGExFBMVariant::Hybrid:
		Value = GenerateHybrid(TransformPosition(Position)) * 0.5 + 0.5;
		break;
	case EPCGExFBMVariant::Warped:
		Value = GenerateWarped(TransformPosition(Position)) * 0.5 + 0.5;
		break;
	default:
		Value = GenerateStandard(TransformPosition(Position)) * 0.5 + 0.5;
	}

	return ApplyRemap(Value);
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryFBM::CreateOperationInternal(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseFBM)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;
	NewOperation->Variant = Config.Variant;
	NewOperation->RidgeOffset = Config.RidgeOffset;
	NewOperation->WarpStrength = Config.WarpStrength;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(FBM, {})

UPCGExFactoryData* UPCGExNoise3DFBMProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryFBM* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryFBM>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
