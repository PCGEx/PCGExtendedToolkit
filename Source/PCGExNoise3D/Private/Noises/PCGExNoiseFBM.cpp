// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseFBM.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseFBM::BaseNoise(const FVector& Position) const
{
	// Perlin noise as base
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

double FPCGExNoiseFBM::GenerateStandard(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	const double Bounding = CalcFractalBounding(Octaves, Persistence);

	for (int32 i = 0; i < Octaves; ++i)
	{
		Sum += BaseNoise(Position * Freq) * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * Bounding;
}

double FPCGExNoiseFBM::GenerateRidged(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	double Weight = 1.0;

	for (int32 i = 0; i < Octaves; ++i)
	{
		double Noise = BaseNoise(Position * Freq);
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
	const double Bounding = CalcFractalBounding(Octaves, Persistence);

	for (int32 i = 0; i < Octaves; ++i)
	{
		double Noise = BaseNoise(Position * Freq);
		Noise = FMath::Abs(Noise) * 2.0 - 1.0;
		Sum += Noise * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * Bounding;
}

double FPCGExNoiseFBM::GenerateHybrid(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	double Weight = 1.0;

	double Noise = (BaseNoise(Position * Freq) + RidgeOffset) * Amp;
	Sum = Noise;
	Weight = Noise;
	Amp *= Persistence;
	Freq *= Lacunarity;

	for (int32 i = 1; i < Octaves; ++i)
	{
		Weight = FMath::Clamp(Weight, 0.0, 1.0);
		Noise = (BaseNoise(Position * Freq) + RidgeOffset) * Amp * Weight;
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
		BaseNoise(Position * WarpFreq),
		BaseNoise((Position + FVector(5.2, 1.3, 2.8)) * WarpFreq),
		BaseNoise((Position + FVector(1.7, 9.2, 3.1)) * WarpFreq)
	);

	const FVector WarpedPos = Position + Warp1 * WarpStrength;

	// Second warp layer
	const FVector Warp2(
		BaseNoise((WarpedPos + FVector(1.7, 9.2, 3.1)) * WarpFreq),
		BaseNoise((WarpedPos + FVector(8.3, 2.8, 4.7)) * WarpFreq),
		BaseNoise((WarpedPos + FVector(2.1, 6.4, 1.8)) * WarpFreq)
	);

	const FVector FinalPos = WarpedPos + Warp2 * WarpStrength;

	// Standard fBm on warped position
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	const double Bounding = CalcFractalBounding(Octaves, Persistence);

	for (int32 i = 0; i < Octaves; ++i)
	{
		Sum += BaseNoise(FinalPos * Freq) * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return Sum * Bounding;
}

double FPCGExNoiseFBM::GenerateRaw(const FVector& Position) const
{
	// Not used - we override GetDouble instead
	return BaseNoise(Position);
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

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryFBM::CreateOperation(FPCGExContext* InContext) const
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
