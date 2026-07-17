// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseWorley.h"
#include "Containers/PCGExManagedObjects.h"
#include "Helpers/PCGExNoise3DMath.h"

using namespace PCGExNoise3D::Math;

void FPCGExNoiseWorley::PostInitDerived()
{
	// Approximate normalization per distance function
	MaxDist = 1.0;
	if (DistanceFunction == EPCGExWorleyDistanceFunc::EuclideanSq ||
		DistanceFunction == EPCGExWorleyDistanceFunc::Manhattan)
	{
		MaxDist = 3.0;
	}
}

double FPCGExNoiseWorley::GenerateRaw(const FVector& Position) const
{
	const int32 CellX = FastFloor(Position.X);
	const int32 CellY = FastFloor(Position.Y);
	const int32 CellZ = FastFloor(Position.Z);

	double WF1 = TNumericLimits<double>::Max();
	double WF2 = TNumericLimits<double>::Max();
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

				// Get feature point in this cell
				const FVector FeaturePoint = GetCellPoint(NX, NY, NZ, Jitter, Seed);
				const double Dist = CalcDistance(Position, FeaturePoint);

				if (Dist < WF1)
				{
					WF2 = WF1;
					WF1 = Dist;
					WinnerX = NX;
					WinnerY = NY;
					WinnerZ = NZ;
				}
				else if (Dist < WF2)
				{
					WF2 = Dist;
				}
			}
		}
	}

	if (ReturnType == EPCGExWorleyReturnType::CellValue)
	{
		return Hash32ToDouble01(Hash32(WinnerX + Seed, WinnerY, WinnerZ));
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
	default:
		Result = WF1;
	}

	return Result;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactoryWorley::CreateOperationInternal(FPCGExContext* InContext) const
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
