// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoisePerlin.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoisePerlin::GenerateRaw(const FVector& Position) const
{
	return Perlin3D(Position, Seed) * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryPerlin::CreateOperationInternal(FPCGExContext* InContext) const
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
