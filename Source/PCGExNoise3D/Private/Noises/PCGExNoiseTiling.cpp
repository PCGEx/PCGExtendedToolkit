// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseTiling.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseTiling::GenerateRaw(const FVector& Position) const
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

	// Periodic wrapping: one modulo per axis, +1 corners wrap incrementally
	const int32 MX0 = Mod(X0 + Seed, PeriodX);
	const int32 MY0 = Mod(Y0, PeriodY);
	const int32 MZ0 = Mod(Z0, PeriodZ);
	const int32 MX1 = MX0 + 1 == PeriodX ? 0 : MX0 + 1;
	const int32 MY1 = MY0 + 1 == PeriodY ? 0 : MY0 + 1;
	const int32 MZ1 = MZ0 + 1 == PeriodZ ? 0 : MZ0 + 1;

	const int32 AAA = Hash3D(MX0, MY0, MZ0);
	const int32 ABA = Hash3D(MX0, MY1, MZ0);
	const int32 AAB = Hash3D(MX0, MY0, MZ1);
	const int32 ABB = Hash3D(MX0, MY1, MZ1);
	const int32 BAA = Hash3D(MX1, MY0, MZ0);
	const int32 BBA = Hash3D(MX1, MY1, MZ0);
	const int32 BAB = Hash3D(MX1, MY0, MZ1);
	const int32 BBB = Hash3D(MX1, MY1, MZ1);

	// Gradient dot products
	const double G_AAA = GradDot3(AAA, Xf, Yf, Zf);
	const double G_BAA = GradDot3(BAA, Xf - 1.0, Yf, Zf);
	const double G_ABA = GradDot3(ABA, Xf, Yf - 1.0, Zf);
	const double G_BBA = GradDot3(BBA, Xf - 1.0, Yf - 1.0, Zf);
	const double G_AAB = GradDot3(AAB, Xf, Yf, Zf - 1.0);
	const double G_BAB = GradDot3(BAB, Xf - 1.0, Yf, Zf - 1.0);
	const double G_ABB = GradDot3(ABB, Xf, Yf - 1.0, Zf - 1.0);
	const double G_BBB = GradDot3(BBB, Xf - 1.0, Yf - 1.0, Zf - 1.0);

	// Trilinear interpolation
	const double X00 = Lerp(G_AAA, G_BAA, U);
	const double X10 = Lerp(G_ABA, G_BBA, U);
	const double X01 = Lerp(G_AAB, G_BAB, U);
	const double X11 = Lerp(G_ABB, G_BBB, U);

	const double XY0 = Lerp(X00, X10, V);
	const double XY1 = Lerp(X01, X11, V);

	return Lerp(XY0, XY1, W);
}

double FPCGExNoiseTiling::GetDouble(const FVector& Position) const
{
	if (Octaves <= 1)
	{
		return ApplyRemap(GenerateRaw(TransformPosition(Position) * Frequency) * 0.5 + 0.5);
	}

	// For tiling with octaves, we scale frequency instead of changing period
	// This maintains proper tiling while adding detail
	double Sum = 0.0;
	double Amp = 1.0;
	double Freq = Frequency;

	for (int32 i = 0; i < Octaves; ++i)
	{
		Sum += GenerateRaw(Position * Freq) * Amp;
		Amp *= Persistence;
		Freq *= Lacunarity;
	}

	return ApplyRemap(Sum * FractalBounding * 0.5 + 0.5);
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryTiling::CreateOperationInternal(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseTiling)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;
	NewOperation->PeriodX = Config.PeriodX;
	NewOperation->PeriodY = Config.PeriodY;
	NewOperation->PeriodZ = Config.PeriodZ;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Tiling, {})

UPCGExFactoryData* UPCGExNoise3DTilingProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryTiling* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryTiling>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
