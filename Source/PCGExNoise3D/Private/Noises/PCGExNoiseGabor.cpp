// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseGabor.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseGabor::GenerateRaw(const FVector& Position) const
{
	const int32 CellX = FastFloor(Position.X);
	const int32 CellY = FastFloor(Position.Y);
	const int32 CellZ = FastFloor(Position.Z);

	const double K = Frequency * Bandwidth;
	const double A = Bandwidth;

	double Sum = 0.0;
	int32 Count = 0;

	// Search cells within kernel radius
	const int32 SearchRadius = static_cast<int32>(FMath::CeilToInt(KernelRadius));

	for (int32 DZ = -SearchRadius; DZ <= SearchRadius; ++DZ)
	{
		for (int32 DY = -SearchRadius; DY <= SearchRadius; ++DY)
		{
			for (int32 DX = -SearchRadius; DX <= SearchRadius; ++DX)
			{
				const int32 NX = CellX + DX;
				const int32 NY = CellY + DY;
				const int32 NZ = CellZ + DZ;

				// Process impulses in this cell
				for (int32 i = 0; i < ImpulsesPerCell; ++i)
				{
					const FVector ImpulseOffset = RandomImpulse(NX, NY, NZ, i);
					const FVector ImpulsePos = FVector(NX, NY, NZ) + ImpulseOffset;
					const FVector Delta = Position - ImpulsePos;

					// Random weight for this impulse
					const uint32 HW = Hash32(NX + i, NY + i, NZ + Seed);
					const double Weight = Hash32ToDouble01(HW) * 2.0 - 1.0;

					Sum += Weight * GaborKernel(Delta, K, A);
					Count++;
				}
			}
		}
	}

	// Normalize
	return Sum * FMath::Sqrt(1.0 / FMath::Max(Count, 1)) * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryGabor::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseGabor)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Direction = Config.Direction.GetSafeNormal();
	NewOperation->Bandwidth = Config.Bandwidth;
	NewOperation->ImpulsesPerCell = Config.ImpulsesPerCell;
	NewOperation->KernelRadius = Config.KernelRadius;
	NewOperation->Octaves = 1;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Gabor, {})

UPCGExFactoryData* UPCGExNoise3DGaborProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryGabor* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryGabor>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
