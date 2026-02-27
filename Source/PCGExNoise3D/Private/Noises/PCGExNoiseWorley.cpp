// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseWorley.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

double FPCGExNoiseWorley::GenerateRaw(const FVector& Position) const
{
	const int32 CellX = FastFloor(Position.X);
	const int32 CellY = FastFloor(Position.Y);
	const int32 CellZ = FastFloor(Position.Z);

	double WF1 = TNumericLimits<double>::Max();
	double WF2 = TNumericLimits<double>::Max();
	double CellVal = 0.0;

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

				// Get feature point in this cell
				const FVector FeaturePoint = GetCellPoint(NX, NY, NZ, Jitter, Seed);
				const double Dist = CalcDistance(Position, FeaturePoint);

				if (Dist < WF1)
				{
					WF2 = WF1;
					WF1 = Dist;
					CellVal = Hash32ToDouble01(Hash32(NX + Seed, NY, NZ));
				}
				else if (Dist < WF2)
				{
					WF2 = Dist;
				}
			}
		}
	}

	// Normalize distances (approximate for different distance functions)
	double MaxDist = 1.0;
	if (DistanceFunction == EPCGExWorleyDistanceFunc::EuclideanSq)
	{
		MaxDist = 3.0;
	}
	else if (DistanceFunction == EPCGExWorleyDistanceFunc::Manhattan)
	{
		MaxDist = 3.0;
	}

	WF1 = FMath::Min(WF1 / MaxDist, 1.0);
	WF2 = FMath::Min(WF2 / MaxDist, 1.0);

	double Result;
	switch (ReturnType)
	{
	case EPCGExWorleyReturnType::F1:
		Result = WF1;
		break;
	case EPCGExWorleyReturnType::F2:
		Result = WF2;
		break;
	case EPCGExWorleyReturnType::F2MinusF1:
		Result = WF2 - WF1;
		break;
	case EPCGExWorleyReturnType::F1PlusF2:
		Result = (WF1 + WF2) * 0.5;
		break;
	case EPCGExWorleyReturnType::F1TimesF2:
		Result = WF1 * WF2;
		break;
	case EPCGExWorleyReturnType::CellValue:
		Result = CellVal;
		break;
	default:
		Result = WF1;
	}

	return Result;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryWorley::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseWorley)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->DistanceFunction = Config.DistanceFunction;
	NewOperation->ReturnType = Config.ReturnType;
	NewOperation->Jitter = Config.Jitter;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Worley, {})

UPCGExFactoryData* UPCGExNoise3DWorleyProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactoryWorley* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactoryWorley>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
