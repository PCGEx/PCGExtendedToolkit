// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Noises/PCGExNoiseSpots.h"
#include "Helpers/PCGExNoise3DMath.h"
#include "Containers/PCGExManagedObjects.h"

using namespace PCGExNoise3D::Math;

FVector FPCGExNoiseSpots::GetSpotCenter(const int32 CellX, const int32 CellY, const int32 CellZ) const
{
	const uint32 H1 = Hash32(CellX + Seed, CellY, CellZ);
	const uint32 H2 = Hash32(CellX, CellY + Seed, CellZ);
	const uint32 H3 = Hash32(CellX, CellY, CellZ + Seed);

	return FVector(
		CellX + 0.5 + (Hash32ToDouble01(H1) - 0.5) * Jitter * 2.0,
		CellY + 0.5 + (Hash32ToDouble01(H2) - 0.5) * Jitter * 2.0,
		CellZ + 0.5 + (Hash32ToDouble01(H3) - 0.5) * Jitter * 2.0
	);
}

double FPCGExNoiseSpots::GetSpotRadius(const int32 CellX, const int32 CellY, const int32 CellZ) const
{
	if (RadiusVariation <= 0.0) { return SpotRadius; }

	const uint32 H = Hash32(CellX + Seed * 3, CellY + Seed * 5, CellZ + Seed * 7);
	const double Variation = (Hash32ToDouble01(H) - 0.5) * 2.0 * RadiusVariation;
	return FMath::Clamp(SpotRadius + Variation, 0.05, 0.9);
}

double FPCGExNoiseSpots::GetSpotValue(const int32 CellX, const int32 CellY, const int32 CellZ) const
{
	if (ValueVariation <= 0.0) { return 1.0; }

	const uint32 H = Hash32(CellX + Seed * 11, CellY + Seed * 13, CellZ + Seed * 17);
	return 1.0 - Hash32ToDouble01(H) * ValueVariation;
}

double FPCGExNoiseSpots::ComputeShapeDistance(const FVector& Offset, const double Radius) const
{
	switch (Shape)
	{
	case EPCGExSpotsShape::Circle:
		{
			const double Dist = Offset.Size() / Radius;
			return Dist <= 1.0 ? 0.0 : 1.0;
		}

	case EPCGExSpotsShape::SoftCircle:
		{
			const double Dist = Offset.Size() / Radius;
			return FMath::Clamp(Dist, 0.0, 1.0);
		}

	case EPCGExSpotsShape::Square:
		{
			const double MaxCoord = FMath::Max3(FMath::Abs(Offset.X), FMath::Abs(Offset.Y), FMath::Abs(Offset.Z)) / Radius;
			return MaxCoord <= 1.0 ? 0.0 : 1.0;
		}

	case EPCGExSpotsShape::Diamond:
		{
			const double ManhattanDist = (FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z)) / Radius;
			return FMath::Clamp(ManhattanDist / 1.5, 0.0, 1.0);
		}

	case EPCGExSpotsShape::Star:
		{
			// Star shape using modulated distance
			const double Angle = FMath::Atan2(Offset.Y, Offset.X);
			const double StarFactor = 0.5 + 0.5 * FMath::Cos(Angle * 5.0);
			const double EffectiveRadius = Radius * (0.5 + 0.5 * StarFactor);
			const double Dist = FVector2D(Offset.X, Offset.Y).Size() / EffectiveRadius;
			return FMath::Clamp(Dist, 0.0, 1.0);
		}

	default:
		return Offset.Size() / Radius;
	}
}

double FPCGExNoiseSpots::GenerateRaw(const FVector& Position) const
{
	const int32 CellX = FastFloor(Position.X);
	const int32 CellY = FastFloor(Position.Y);
	const int32 CellZ = FastFloor(Position.Z);

	double MinDist = 1.0;
	double SpotVal = 0.0;

	// Check 3x3x3 neighborhood for overlapping spots
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const int32 NX = CellX + DX;
				const int32 NY = CellY + DY;
				const int32 NZ = CellZ + DZ;

				const FVector Center = GetSpotCenter(NX, NY, NZ);
				const double Radius = GetSpotRadius(NX, NY, NZ);
				const FVector Offset = Position - Center;

				const double Dist = ComputeShapeDistance(Offset, Radius);

				if (Dist < MinDist)
				{
					MinDist = Dist;
					SpotVal = GetSpotValue(NX, NY, NZ);
				}
			}
		}
	}

	// Convert distance to value
	double Result;
	if (Shape == EPCGExSpotsShape::Circle || Shape == EPCGExSpotsShape::Square)
	{
		// Hard edge shapes
		Result = MinDist < 0.5 ? SpotVal : 0.0;
	}
	else
	{
		// Soft falloff shapes
		Result = (1.0 - MinDist) * SpotVal;
	}

	if (bInvertSpots)
	{
		Result = 1.0 - Result;
	}

	return Result;
}

TSharedPtr<FPCGExNoise3DOperation> UPCGExNoise3DFactorySpots::CreateOperation(FPCGExContext* InContext) const
{
	PCGEX_FACTORY_NEW_OPERATION(NoiseSpots)
	PCGEX_FORWARD_NOISE3D_CONFIG

	NewOperation->Shape = Config.Shape;
	NewOperation->SpotRadius = Config.SpotRadius;
	NewOperation->RadiusVariation = Config.RadiusVariation;
	NewOperation->Jitter = Config.Jitter;
	NewOperation->bInvertSpots = Config.bInvertSpots;
	NewOperation->ValueVariation = Config.ValueVariation;
	NewOperation->Octaves = 1;

	return NewOperation;
}

PCGEX_NOISE3D_FACTORY_BOILERPLATE_IMPL(Spots, {})

UPCGExFactoryData* UPCGExNoise3DSpotsProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const
{
	UPCGExNoise3DFactorySpots* NewFactory = InContext->ManagedObjects->New<UPCGExNoise3DFactorySpots>();
	PCGEX_FORWARD_NOISE3D_FACTORY
	return Super::CreateFactory(InContext, NewFactory);
}
