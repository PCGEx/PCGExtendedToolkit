// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoisePerlin.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoisePerlin::GenerateRaw(const FVector& Position) const
{
	// Find unit cube containing point
	const int32 X0 = FastFloor(Position.X);
	const int32 Y0 = FastFloor(Position.Y);
	const int32 Z0 = FastFloor(Position.Z);

	// Relative position within cube
	const double Xf = Position.X - X0;
	const double Yf = Position.Y - Y0;
	const double Zf = Position.Z - Z0;

	// Quintic interpolation curves
	const double U = SmoothStep(Xf);
	const double V = SmoothStep(Yf);
	const double W = SmoothStep(Zf);

	// Hash coordinates with seed
	const int32 X0S = (X0 + Seed) & 255;
	const int32 Y0S = Y0 & 255;
	const int32 Z0S = Z0 & 255;

	// Hash all 8 corners
	const int32 AAA = Hash3D(X0S, Y0S, Z0S);
	const int32 ABA = Hash3D(X0S, Y0S + 1, Z0S);
	const int32 AAB = Hash3D(X0S, Y0S, Z0S + 1);
	const int32 ABB = Hash3D(X0S, Y0S + 1, Z0S + 1);
	const int32 BAA = Hash3D(X0S + 1, Y0S, Z0S);
	const int32 BBA = Hash3D(X0S + 1, Y0S + 1, Z0S);
	const int32 BAB = Hash3D(X0S + 1, Y0S, Z0S + 1);
	const int32 BBB = Hash3D(X0S + 1, Y0S + 1, Z0S + 1);

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

	return Lerp(XY0, XY1, W) * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryPerlin::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoisePerlin)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Perlin, {})

UPCGExFactoryData* UPCGExNoise3DPerlinProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryPerlin* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryPerlin>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
