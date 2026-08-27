// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExSmoothingInstancedFactory.h"
#include "Core/PCGExProxyDataBlending.h"
#include "Data/PCGExPointIO.h"
#include "Factories/PCGExFactoryData.h"

#include "PCGExRadiusSmoothing.generated.h"

class FPCGExRadiusSmoothing : public FPCGExSmoothingOperation
{
public:
	virtual void SmoothSingle(const int32 TargetIndex, const double Smoothing, const double Influence, TArray<PCGEx::FOpStats>& Trackers) override
	{
		const double RadiusSquared = Smoothing * Smoothing;

		if (Influence == 0)
		{
			return;
		}

		TConstPCGValueRange<FTransform> InTransforms = Path->GetIn()->GetConstTransformValueRange();

		const FVector Origin = InTransforms[TargetIndex].GetLocation();

		// Gathered before the blend opens: BeginMultiBlend zeroes the target and EndMultiBlend
		// early-returns on a zero contributor count, so opening a blend nothing feeds leaves every
		// channel at zero. The target is excluded from its own query, so an isolated point hits that.
		TArray<TPair<int32, double>, TInlineAllocator<32>> Contributors;

		Path->GetIn()->GetPointOctree().FindElementsWithBoundsTest(FBoxCenterAndExtent(Origin, FVector(Smoothing)), [&](const PCGPointOctree::FPointRef& PointRef)
		{
			const double Dist = FVector::DistSquared(Origin, InTransforms[PointRef.Index].GetLocation());
			if (Dist >= RadiusSquared || PointRef.Index == TargetIndex)
			{
				return;
			}

			Contributors.Emplace(PointRef.Index, (1 - (Dist / RadiusSquared)) * Influence);
		});

		if (Contributors.IsEmpty())
		{
			return;
		}

		Blender->BeginMultiBlend(TargetIndex, Trackers);

		for (const TPair<int32, double>& Contributor : Contributors)
		{
			Blender->MultiBlend(Contributor.Key, TargetIndex, Contributor.Value, Trackers);
		}

		Blender->EndMultiBlend(TargetIndex, Trackers);
	}
};

/**
 * 
 */
UCLASS(MinimalAPI, meta=(DisplayName = "Radius", PCGExNodeLibraryDoc="paths/transform/path-smooth/radius-smoothing"))
class UPCGExRadiusSmoothing : public UPCGExSmoothingInstancedFactory
{
	GENERATED_BODY()

public:
	virtual TSharedPtr<FPCGExSmoothingOperation> CreateOperation() const override
	{
		PCGEX_FACTORY_NEW_OPERATION(RadiusSmoothing)
		return NewOperation;
	}
};
