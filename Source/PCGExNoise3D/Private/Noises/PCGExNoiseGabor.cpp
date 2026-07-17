// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseGabor.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

void FPCGExNoiseGabor::PostInitDerived()
{
	SearchRadius = static_cast<int32>(FMath::CeilToInt(KernelRadius));
	KernelRadiusSq = KernelRadius * KernelRadius;
	GaussCoeff = -PI * Bandwidth * Bandwidth;
	PhaseCoeff = 2.0 * PI * (Frequency * Bandwidth);

	// Impulse count is deterministic: every cell in the search cube contributes ImpulsesPerCell
	const int32 CellsPerAxis = 2 * SearchRadius + 1;
	const int32 TotalImpulses = ImpulsesPerCell * CellsPerAxis * CellsPerAxis * CellsPerAxis;
	Normalization = FMath::Sqrt(1.0 / FMath::Max(TotalImpulses, 1));
}

double FPCGExNoiseGabor::GenerateRaw(const FVector& Position) const
{
	const int32 CellX = FastFloor(Position.X);
	const int32 CellY = FastFloor(Position.Y);
	const int32 CellZ = FastFloor(Position.Z);

	double Sum = 0.0;

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
					double Weight = 0.0;
					const FVector ImpulseOffset = RandomImpulse(NX, NY, NZ, i, Weight);
					const FVector ImpulsePos = FVector(NX, NY, NZ) + ImpulseOffset;
					const FVector Delta = Position - ImpulsePos;

					Sum += Weight * GaborKernel(Delta);
				}
			}
		}
	}

	return Sum * Normalization * 0.5 + 0.5;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryGabor::CreateOperationInternal(FPCGExContext* InContext) const
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
