// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseCurl.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseCurl::BaseNoise(const FVector& Position) const
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

FVector FPCGExNoiseCurl::GetPotentialField(const FVector& Position) const
{
	// Three independent noise channels for potential field
	const FVector ScaledPos = Position * Frequency;
	const double Fx = BaseNoise(ScaledPos);
	const double Fy = BaseNoise(ScaledPos + FVector(31.416, 47.853, 12.793));
	const double Fz = BaseNoise(ScaledPos + FVector(93.139, 25.186, 71.524));
	return FVector(Fx, Fy, Fz);
}

FVector FPCGExNoiseCurl::ComputeCurl(const FVector& Position) const
{
	const double E = Epsilon;
	const double InvE2 = 1.0 / (2.0 * E);

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

double FPCGExNoiseCurl::GenerateRaw(const FVector& Position) const
{
	// For scalar output, return magnitude of curl
	const FVector ScaledPos = Position * Frequency;

	// Need a non-const way to compute curl
	const double E = Epsilon;
	const double InvE2 = 1.0 / (2.0 * E);

	const FVector PxP = GetPotentialField(ScaledPos + FVector(E, 0, 0));
	const FVector PxN = GetPotentialField(ScaledPos - FVector(E, 0, 0));
	const FVector PyP = GetPotentialField(ScaledPos + FVector(0, E, 0));
	const FVector PyN = GetPotentialField(ScaledPos - FVector(0, E, 0));
	const FVector PzP = GetPotentialField(ScaledPos + FVector(0, 0, E));
	const FVector PzN = GetPotentialField(ScaledPos - FVector(0, 0, E));

	const double dFz_dy = (PyP.Z - PyN.Z) * InvE2;
	const double dFy_dz = (PzP.Y - PzN.Y) * InvE2;
	const double dFx_dz = (PzP.X - PzN.X) * InvE2;
	const double dFz_dx = (PxP.Z - PxN.Z) * InvE2;
	const double dFy_dx = (PxP.Y - PxN.Y) * InvE2;
	const double dFx_dy = (PyP.X - PyN.X) * InvE2;

	const FVector Curl(
		dFz_dy - dFy_dz,
		dFx_dz - dFz_dx,
		dFy_dx - dFx_dy
	);

	return Curl.Size() * CurlScale;
}

double FPCGExNoiseCurl::GetDouble(const FVector& Position) const
{
	return GetVector(Position).X;
}

FVector2D FPCGExNoiseCurl::GetVector2D(const FVector& Position) const
{
	const FVector Curl = GetVector(Position);
	return FVector2D(Curl);
}

FVector FPCGExNoiseCurl::GetVector(const FVector& Position) const
{
	FVector Curl = ComputeCurl(TransformPosition(Position));

	// Apply fractal octaves if configured
	if (Octaves > 1)
	{
		double Amp = 1.0;
		double Freq = 1.0;
		const double Bounding = CalcFractalBounding(Octaves, Persistence);

		Curl *= Amp;

		for (int32 i = 1; i < Octaves; ++i)
		{
			Amp *= Persistence;
			Freq *= Lacunarity;
			Curl += ComputeCurl(Position * Freq) * Amp;
		}

		Curl *= Bounding;
	}

	for (int i = 0; i < 3; i++) { Curl[i] = ApplyRemap(Curl[i] * 0.5 + 0.5); }

	return Curl;
}

FVector4 FPCGExNoiseCurl::GetVector4(const FVector& Position) const
{
	const FVector Curl = GetVector(Position);
	return FVector4(Curl.X, Curl.Y, Curl.Z, Curl.Size());
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryCurl::CreateOperation(FPCGExContext* InContext) const
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
