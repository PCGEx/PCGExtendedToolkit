// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseCaustic.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseCaustic::GenerateWaveLayer(const FVector& Position, const int32 LayerIndex) const
{
	// Each layer has a different angle and phase
	const double AngleOffset = LayerIndex * PI * 2.0 / WaveLayers;
	const double PhaseOffset = LayerIndex * 1.7;
	const double TimeOffset = Time * AnimationSpeed + PhaseOffset;

	// Create wave direction from angle
	const double CosA = FMath::Cos(AngleOffset);
	const double SinA = FMath::Sin(AngleOffset);

	// Project position onto wave direction
	const double ProjXY = Position.X * CosA + Position.Y * SinA;
	const double ProjXZ = Position.X * SinA + Position.Z * CosA;

	// Multiple overlapping sine waves for complexity
	const double Wave1 = FMath::Sin((ProjXY / Wavelength + TimeOffset) * PI * 2.0);
	const double Wave2 = FMath::Sin((ProjXZ / Wavelength * 0.7 + TimeOffset * 1.3) * PI * 2.0);
	const double Wave3 = FMath::Sin(((ProjXY + ProjXZ) / Wavelength * 0.5 + TimeOffset * 0.8) * PI * 2.0);

	return (Wave1 + Wave2 * 0.7 + Wave3 * 0.5) / 2.2;
}

double FPCGExNoiseCaustic::GenerateRaw(const FVector& Position) const
{
	const FVector ScaledPos = Position * Frequency;

	double Sum = 0.0;
	for (int32 i = 0; i < WaveLayers; ++i)
	{
		Sum += GenerateWaveLayer(ScaledPos, i);
	}

	// Normalize
	Sum /= WaveLayers;

	// Convert to [0, 1] range
	Sum = Sum * 0.5 + 0.5;

	// Apply focus (power function creates bright focal points)
	Sum = FMath::Pow(Sum, Focus);

	// Apply intensity
	Sum *= Intensity;

	return FMath::Clamp(Sum, 0.0, 1.0);
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryCaustic::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseCaustic)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->WaveLayers = Config.WaveLayers;
	NewOperation->Wavelength = Config.Wavelength;
	NewOperation->Time = Config.Time;
	NewOperation->AnimationSpeed = Config.AnimationSpeed;
	NewOperation->Intensity = Config.Intensity;
	NewOperation->Focus = Config.Focus;
	NewOperation->Octaves = 1;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Caustic, {})

UPCGExFactoryData* UPCGExNoise3DCausticProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryCaustic* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryCaustic>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
