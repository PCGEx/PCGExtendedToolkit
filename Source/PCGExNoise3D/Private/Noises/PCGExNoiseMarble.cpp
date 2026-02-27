// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseMarble.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseMarble::BaseNoise(const FVector& Position) const
{
	const int32 X0 = FastFloor(Position.X);
	const int32 Y0 = FastFloor(Position.Y);
	const int32 Z0 = FastFloor(Position.Z);

	const double Xf = Position.X - X0;
	const double Yf = Position.Y - Y0;
	const double Zf = Position.Z - Z0;

	const double U = SmoothStep(Xf);
	const double V = SmoothStep(Yf);
	const double W = SmoothStep(Zf);

	const int32 X0S = (X0 + Seed) & 255;

	const int32 AAA = Hash3D(X0S, Y0, Z0);
	const int32 ABA = Hash3D(X0S, Y0 + 1, Z0);
	const int32 AAB = Hash3D(X0S, Y0, Z0 + 1);
	const int32 ABB = Hash3D(X0S, Y0 + 1, Z0 + 1);
	const int32 BAA = Hash3D(X0S + 1, Y0, Z0);
	const int32 BBA = Hash3D(X0S + 1, Y0 + 1, Z0);
	const int32 BAB = Hash3D(X0S + 1, Y0, Z0 + 1);
	const int32 BBB = Hash3D(X0S + 1, Y0 + 1, Z0 + 1);

	const double G_AAA = GradDot3(AAA, Xf, Yf, Zf);
	const double G_BAA = GradDot3(BAA, Xf - 1.0, Yf, Zf);
	const double G_ABA = GradDot3(ABA, Xf, Yf - 1.0, Zf);
	const double G_BBA = GradDot3(BBA, Xf - 1.0, Yf - 1.0, Zf);
	const double G_AAB = GradDot3(AAB, Xf, Yf, Zf - 1.0);
	const double G_BAB = GradDot3(BAB, Xf - 1.0, Yf, Zf - 1.0);
	const double G_ABB = GradDot3(ABB, Xf, Yf - 1.0, Zf - 1.0);
	const double G_BBB = GradDot3(BBB, Xf - 1.0, Yf - 1.0, Zf - 1.0);

	const double X00 = Lerp(G_AAA, G_BAA, U);
	const double X10 = Lerp(G_ABA, G_BBA, U);
	const double X01 = Lerp(G_AAB, G_BAB, U);
	const double X11 = Lerp(G_ABB, G_BBB, U);

	const double XY0 = Lerp(X00, X10, V);
	const double XY1 = Lerp(X01, X11, V);

	return Lerp(XY0, XY1, W);
}

double FPCGExNoiseMarble::GenerateTurbulence(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = 1.0;
	double MaxVal = 0.0;

	for (int32 i = 0; i < TurbulenceOctaves; ++i)
	{
		Sum += FMath::Abs(BaseNoise(Position * Freq)) * Amp;
		MaxVal += Amp;
		Amp *= 0.5;
		Freq *= 2.0;
	}

	return Sum / MaxVal;
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

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryMarble::CreateOperation(FPCGExContext* InContext) const
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
