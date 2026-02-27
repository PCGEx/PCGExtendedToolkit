// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseWhite.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseWhite::GenerateRaw(const FVector& Position) const
{
	// Hash position directly - no interpolation
	const int32 X = FastFloor(Position.X);
	const int32 Y = FastFloor(Position.Y);
	const int32 Z = FastFloor(Position.Z);

	const uint32 H = Hash32(X + Seed, Y, Z);
	return Hash32ToDouble01(H);
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryWhite::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseWhite)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Octaves = 1; // White noise doesn't use octaves

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(White, {})

UPCGExFactoryData* UPCGExNoise3DWhiteProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryWhite* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryWhite>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
