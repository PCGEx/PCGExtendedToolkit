// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseValue.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseValue::GenerateRaw(const FVector& Position) const
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

	// Get values at 8 corners
	const double V000 = HashToDouble(Hash3D(X0S, Y0, Z0));
	const double V100 = HashToDouble(Hash3D(X0S + 1, Y0, Z0));
	const double V010 = HashToDouble(Hash3D(X0S, Y0 + 1, Z0));
	const double V110 = HashToDouble(Hash3D(X0S + 1, Y0 + 1, Z0));
	const double V001 = HashToDouble(Hash3D(X0S, Y0, Z0 + 1));
	const double V101 = HashToDouble(Hash3D(X0S + 1, Y0, Z0 + 1));
	const double V011 = HashToDouble(Hash3D(X0S, Y0 + 1, Z0 + 1));
	const double V111 = HashToDouble(Hash3D(X0S + 1, Y0 + 1, Z0 + 1));

	// Trilinear interpolation
	const double X00 = Lerp(V000, V100, U);
	const double X10 = Lerp(V010, V110, U);
	const double X01 = Lerp(V001, V101, U);
	const double X11 = Lerp(V011, V111, U);

	const double XY0 = Lerp(X00, X10, V);
	const double XY1 = Lerp(X01, X11, V);

	return Lerp(XY0, XY1, W);
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryValue::CreateOperationInternal(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseValue)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = Config.Octaves;
	NewOperation->Lacunarity = Config.Lacunarity;
	NewOperation->Persistence = Config.Persistence;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Value, {})

UPCGExFactoryData* UPCGExNoise3DValueProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryValue* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryValue>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
