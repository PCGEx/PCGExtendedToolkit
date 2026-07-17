// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseVoronoi.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseVoronoi::GenerateRaw(const FVector& Position) const
{
	const int32 CellX = FastFloor(Position.X);
	const int32 CellY = FastFloor(Position.Y);
	const int32 CellZ = FastFloor(Position.Z);

	double VF1 = TNumericLimits<double>::Max();
	double VF2 = TNumericLimits<double>::Max();

	if (Smoothness > 0.0)
	{
		// Smooth blend only tracks VF1; VF2 keeps its default (matches historical behavior)
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const FVector FeaturePoint = GetCellPoint(CellX + DX, CellY + DY, CellZ + DZ, Jitter, Seed);
					VF1 = SmoothMin(VF1, FVector::Dist(Position, FeaturePoint), Smoothness);
				}
			}
		}
	}
	else
	{
		int32 WinnerX = CellX;
		int32 WinnerY = CellY;
		int32 WinnerZ = CellZ;

		// Search 3x3x3 neighborhood
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const int32 NX = CellX + DX;
					const int32 NY = CellY + DY;
					const int32 NZ = CellZ + DZ;

					const FVector FeaturePoint = GetCellPoint(NX, NY, NZ, Jitter, Seed);
					const double Dist = FVector::Dist(Position, FeaturePoint);

					if (Dist < VF1)
					{
						VF2 = VF1;
						VF1 = Dist;
						WinnerX = NX;
						WinnerY = NY;
						WinnerZ = NZ;
					}
					else if (Dist < VF2)
					{
						VF2 = Dist;
					}
				}
			}
		}

		if (OutputMode == EPCGExVoronoiOutput::CellValue)
		{
			return Hash32ToDouble01(Hash32(WinnerX + Seed, WinnerY, WinnerZ));
		}
	}

	// Normalize distances
	VF1 = FMath::Clamp(VF1, 0.0, 1.0);
	VF2 = FMath::Clamp(VF2, 0.0, 1.5);

	double Result;
	switch (OutputMode)
	{
	case EPCGExVoronoiOutput::Distance:
		Result = VF1;
		break;
	case EPCGExVoronoiOutput::EdgeDistance:
	{
		// Approximate edge distance
		const double Edge = (VF2 - VF1) * 0.5;
		Result = 1.0 - FMath::Clamp(Edge * 2.0, 0.0, 1.0);
	}
	break;
	case EPCGExVoronoiOutput::Crackle:
		Result = VF2 - VF1;
		break;
	default:
		// CellValue in smooth mode has no winner to hash (historical behavior)
		Result = 0.0;
	}

	return Result;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryVoronoi::CreateOperationInternal(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseVoronoi)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->OutputMode = Config.OutputType;
	NewOperation->Jitter = Config.Jitter;
	NewOperation->Smoothness = Config.Smoothness;
	NewOperation->Octaves = 1;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Voronoi, {})

UPCGExFactoryData* UPCGExNoise3DVoronoiProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryVoronoi* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryVoronoi>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
