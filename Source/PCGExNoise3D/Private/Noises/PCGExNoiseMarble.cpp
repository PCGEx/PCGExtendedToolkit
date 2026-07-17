// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseMarble.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

void FPCGExNoiseMarble::PostInitDerived()
{
	// Property clamps don't apply to override-pin values; <= 0 octaves would divide by zero below
	TurbulenceOctaves = FMath::Clamp(TurbulenceOctaves, 1, 16);

	double Amp = 1.0;
	double MaxVal = 0.0;
	for (int32 i = 0; i < TurbulenceOctaves; ++i)
	{
		MaxVal += Amp;
		Amp *= 0.5;
	}
	InvTurbulenceNorm = 1.0 / MaxVal;
}

double FPCGExNoiseMarble::GenerateTurbulence(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = 1.0;

	for (int32 i = 0; i < TurbulenceOctaves; ++i)
	{
		Sum += FMath::Abs(Perlin3D(Position * Freq, Seed)) * Amp;
		Amp *= 0.5;
		Freq *= 2.0;
	}

	return Sum * InvTurbulenceNorm;
}

double FPCGExNoiseMarble::GenerateRaw(const FVector& Position) const
{
	// Get base coordinate for sine wave
	double BaseCoord;
	switch (Direction)
	{
	case EPCGExMarbleDirection::X:
		BaseCoord = Position.X;
		break;
	case EPCGExMarbleDirection::Y:
		BaseCoord = Position.Y;
		break;
	case EPCGExMarbleDirection::Z:
		BaseCoord = Position.Z;
		break;
	case EPCGExMarbleDirection::Radial:
		BaseCoord = Position.Size();
		break;
	default:
		BaseCoord = Position.X;
	}

	// Apply turbulence distortion
	const double Turbulence = GenerateTurbulence(Position * Frequency) * TurbulenceStrength;

	// Create marble pattern with sine wave
	const double SineInput = (BaseCoord * VeinFrequency + Turbulence) * PI;
	double Result = FMath::Sin(SineInput);

	// Apply sharpness
	if (VeinSharpness > 1.0)
	{
		Result = FMath::Sign(Result) * FMath::Pow(FMath::Abs(Result), 1.0 / VeinSharpness);
	}

	return Result * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryMarble::CreateOperationInternal(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseMarble)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Direction = Config.Direction;
	NewOperation->VeinFrequency = Config.VeinFrequency;
	NewOperation->TurbulenceStrength = Config.TurbulenceStrength;
	NewOperation->TurbulenceOctaves = Config.TurbulenceOctaves;
	NewOperation->VeinSharpness = Config.VeinSharpness;
	NewOperation->Octaves = 1; // Marble uses internal turbulence

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Marble, {})

UPCGExFactoryData* UPCGExNoise3DMarbleProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryMarble* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryMarble>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
