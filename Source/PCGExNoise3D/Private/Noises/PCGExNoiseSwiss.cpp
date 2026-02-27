// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseSwiss.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

void FPCGExNoiseSwiss::NoiseWithDerivatives(const FVector& Position, double& OutValue, FVector& OutDerivatives) const
{
	const int32 X0 = FastFloor(Position.X);
	const int32 Y0 = FastFloor(Position.Y);
	const int32 Z0 = FastFloor(Position.Z);

	const double Xf = Position.X - X0;
	const double Yf = Position.Y - Y0;
	const double Zf = Position.Z - Z0;

	// Quintic interpolation
	const double U = SmoothStep(Xf);
	const double V = SmoothStep(Yf);
	const double W = SmoothStep(Zf);

	// Derivatives of quintic
	const double DU = SmoothStepDeriv(Xf);
	const double DV = SmoothStepDeriv(Yf);
	const double DW = SmoothStepDeriv(Zf);

	const int32 X0S = (X0 + Seed) & 255;

	// Hash all 8 corners
	const int32 AAA = Hash3D(X0S, Y0, Z0);
	const int32 ABA = Hash3D(X0S, Y0 + 1, Z0);
	const int32 AAB = Hash3D(X0S, Y0, Z0 + 1);
	const int32 ABB = Hash3D(X0S, Y0 + 1, Z0 + 1);
	const int32 BAA = Hash3D(X0S + 1, Y0, Z0);
	const int32 BBA = Hash3D(X0S + 1, Y0 + 1, Z0);
	const int32 BAB = Hash3D(X0S + 1, Y0, Z0 + 1);
	const int32 BBB = Hash3D(X0S + 1, Y0 + 1, Z0 + 1);

	// Gradient dot products
	const double G_AAA = GradDot3(AAA, Xf, Yf, Zf);
	const double G_BAA = GradDot3(BAA, Xf - 1.0, Yf, Zf);
	const double G_ABA = GradDot3(ABA, Xf, Yf - 1.0, Zf);
	const double G_BBA = GradDot3(BBA, Xf - 1.0, Yf - 1.0, Zf);
	const double G_AAB = GradDot3(AAB, Xf, Yf, Zf - 1.0);
	const double G_BAB = GradDot3(BAB, Xf - 1.0, Yf, Zf - 1.0);
	const double G_ABB = GradDot3(ABB, Xf, Yf - 1.0, Zf - 1.0);
	const double G_BBB = GradDot3(BBB, Xf - 1.0, Yf - 1.0, Zf - 1.0);

	// Interpolation coefficients
	const double K0 = G_AAA;
	const double K1 = G_BAA - G_AAA;
	const double K2 = G_ABA - G_AAA;
	const double K3 = G_AAB - G_AAA;
	const double K4 = G_AAA - G_BAA - G_ABA + G_BBA;
	const double K5 = G_AAA - G_ABA - G_AAB + G_ABB;
	const double K6 = G_AAA - G_BAA - G_AAB + G_BAB;
	const double K7 = G_BAA + G_ABA + G_AAB - G_AAA - G_BBA - G_ABB - G_BAB + G_BBB;

	// Value
	OutValue = K0 + K1 * U + K2 * V + K3 * W + K4 * U * V + K5 * V * W + K6 * U * W + K7 * U * V * W;

	// Gradient contributions from corners
	const FVector GA = GetGrad3(AAA);
	const FVector GB = GetGrad3(BAA);
	const FVector GC = GetGrad3(ABA);
	const FVector GD = GetGrad3(BBA);
	const FVector GE = GetGrad3(AAB);
	const FVector GF = GetGrad3(BAB);
	const FVector GG = GetGrad3(ABB);
	const FVector GH = GetGrad3(BBB);

	// Analytical derivatives
	OutDerivatives.X = GA.X * (1 - U) * (1 - V) * (1 - W) + GB.X * U * (1 - V) * (1 - W) +
		GC.X * (1 - U) * V * (1 - W) + GD.X * U * V * (1 - W) +
		GE.X * (1 - U) * (1 - V) * W + GF.X * U * (1 - V) * W +
		GG.X * (1 - U) * V * W + GH.X * U * V * W +
		DU * (K1 + K4 * V + K6 * W + K7 * V * W);

	OutDerivatives.Y = GA.Y * (1 - U) * (1 - V) * (1 - W) + GB.Y * U * (1 - V) * (1 - W) +
		GC.Y * (1 - U) * V * (1 - W) + GD.Y * U * V * (1 - W) +
		GE.Y * (1 - U) * (1 - V) * W + GF.Y * U * (1 - V) * W +
		GG.Y * (1 - U) * V * W + GH.Y * U * V * W +
		DV * (K2 + K4 * U + K5 * W + K7 * U * W);

	OutDerivatives.Z = GA.Z * (1 - U) * (1 - V) * (1 - W) + GB.Z * U * (1 - V) * (1 - W) +
		GC.Z * (1 - U) * V * (1 - W) + GD.Z * U * V * (1 - W) +
		GE.Z * (1 - U) * (1 - V) * W + GF.Z * U * (1 - V) * W +
		GG.Z * (1 - U) * V * W + GH.Z * U * V * W +
		DW * (K3 + K5 * V + K6 * U + K7 * U * V);
}

double FPCGExNoiseSwiss::GenerateRaw(const FVector& Position) const
{
	// For non-fractal use
	double Value;
	FVector Deriv;
	NoiseWithDerivatives(Position, Value, Deriv);
	return Value;
}

double FPCGExNoiseSwiss::GetDouble(const FVector& Position) const
{
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;
	FVector DerivSum = FVector::ZeroVector;

	const double Bounding = CalcFractalBounding(Octaves, Persistence);

	for (int32 i = 0; i < Octaves; ++i)
	{
		// Warp position based on accumulated derivatives
		const FVector WarpedPos = Position * Freq + DerivSum * WarpFactor;

		double Value;
		FVector Deriv;
		NoiseWithDerivatives(WarpedPos, Value, Deriv);

		// Reduce amplitude based on derivative magnitude (erosion effect)
		const double DerivMag = DerivSum.Size();
		const double ErosionFactor = 1.0 / (1.0 + DerivMag * DerivMag * ErosionStrength);

		Sum += Value * Amp * ErosionFactor;
		DerivSum += Deriv * Amp * Freq;

		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return ApplyRemap(Sum * Bounding * 0.5 + 0.5);
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactorySwiss::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseSwiss)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;
	NewOperation->ErosionStrength = Config.ErosionStrength;
	NewOperation->WarpFactor = Config.WarpFactor;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Swiss, {})

UPCGExFactoryData* UPCGExNoise3DSwissProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactorySwiss* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactorySwiss>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
