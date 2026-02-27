// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseFlow.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseFlow::GenerateRaw(const FVector& Position) const
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

	// Hash all 8 corners
	const int32 AAA = Hash3D(X0S, Y0, Z0);
	const int32 ABA = Hash3D(X0S, Y0 + 1, Z0);
	const int32 AAB = Hash3D(X0S, Y0, Z0 + 1);
	const int32 ABB = Hash3D(X0S, Y0 + 1, Z0 + 1);
	const int32 BAA = Hash3D(X0S + 1, Y0, Z0);
	const int32 BBA = Hash3D(X0S + 1, Y0 + 1, Z0);
	const int32 BAB = Hash3D(X0S + 1, Y0, Z0 + 1);
	const int32 BBB = Hash3D(X0S + 1, Y0 + 1, Z0 + 1);

	// Get rotated gradients
	const FVector G_AAA = GetRotatedGradient(AAA, Time);
	const FVector G_BAA = GetRotatedGradient(BAA, Time);
	const FVector G_ABA = GetRotatedGradient(ABA, Time);
	const FVector G_BBA = GetRotatedGradient(BBA, Time);
	const FVector G_AAB = GetRotatedGradient(AAB, Time);
	const FVector G_BAB = GetRotatedGradient(BAB, Time);
	const FVector G_ABB = GetRotatedGradient(ABB, Time);
	const FVector G_BBB = GetRotatedGradient(BBB, Time);

	// Dot products with rotated gradients
	const double D_AAA = FVector::DotProduct(G_AAA, FVector(Xf, Yf, Zf));
	const double D_BAA = FVector::DotProduct(G_BAA, FVector(Xf - 1.0, Yf, Zf));
	const double D_ABA = FVector::DotProduct(G_ABA, FVector(Xf, Yf - 1.0, Zf));
	const double D_BBA = FVector::DotProduct(G_BBA, FVector(Xf - 1.0, Yf - 1.0, Zf));
	const double D_AAB = FVector::DotProduct(G_AAB, FVector(Xf, Yf, Zf - 1.0));
	const double D_BAB = FVector::DotProduct(G_BAB, FVector(Xf - 1.0, Yf, Zf - 1.0));
	const double D_ABB = FVector::DotProduct(G_ABB, FVector(Xf, Yf - 1.0, Zf - 1.0));
	const double D_BBB = FVector::DotProduct(G_BBB, FVector(Xf - 1.0, Yf - 1.0, Zf - 1.0));

	// Trilinear interpolation
	const double X00 = Lerp(D_AAA, D_BAA, U);
	const double X10 = Lerp(D_ABA, D_BBA, U);
	const double X01 = Lerp(D_AAB, D_BAB, U);
	const double X11 = Lerp(D_ABB, D_BBB, U);

	const double XY0 = Lerp(X00, X10, V);
	const double XY1 = Lerp(X01, X11, V);

	return Lerp(XY0, XY1, W) * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryFlow::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseFlow)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;
	NewOperation->Time = Config.Time;
	NewOperation->RotationSpeed = Config.RotationSpeed;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Flow, {})

UPCGExFactoryData* UPCGExNoise3DFlowProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryFlow* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryFlow>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
