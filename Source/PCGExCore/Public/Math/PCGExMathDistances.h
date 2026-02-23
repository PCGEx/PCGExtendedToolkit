// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"

namespace PCGExData
{
	struct FPoint;
}

namespace PCGExMath
{
	template <EPCGExDistance Mode, typename PointType>
	static FVector GetSpatializedCenter(const PointType& FromPoint, const FVector& FromCenter, const FVector& ToCenter)
	{
		if constexpr (Mode == EPCGExDistance::None)
		{
			return FVector::OneVector;
		}
		else if constexpr (Mode == EPCGExDistance::SphereBounds)
		{
			return FromCenter + (ToCenter - FromCenter).GetSafeNormal() * FromPoint.GetScaledExtents().Length();
		}
		else if constexpr (Mode == EPCGExDistance::BoxBounds)
		{
			const FVector LocalTargetCenter = FromPoint.GetTransform().InverseTransformPosition(ToCenter);
			const FVector LocalClosestPoint = LocalTargetCenter.ComponentMax(FromPoint.GetBoundsMin()).ComponentMin(FromPoint.GetBoundsMax());

			return FromPoint.GetTransform().TransformPosition(LocalClosestPoint);
		}
		else
		{
			return FromCenter;
		}
	}

	class PCGEXCORE_API IDistances : public TSharedFromThis<IDistances>
	{
	public:
		virtual ~IDistances() = default;

		bool bOverlapIsZero = false;

		IDistances() = default;

		explicit IDistances(const bool InOverlapIsZero)
			: bOverlapIsZero(InOverlapIsZero)
		{
		}

		virtual FVector GetSourceCenter(const PCGExData::FPoint& OriginPoint, const FVector& OriginLocation, const FVector& ToCenter) const = 0;
		virtual FVector GetTargetCenter(const PCGExData::FPoint& OriginPoint, const FVector& OriginLocation, const FVector& ToCenter) const = 0;
		virtual void GetCenters(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint, FVector& OutSource, FVector& OutTarget) const = 0;

		virtual double GetDistSquared(const FVector& SourcePos, const FVector& TargetPos) const = 0;
		virtual double GetDist(const FVector& SourcePos, const FVector& TargetPos) const = 0;

		virtual double GetDistSquared(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint) const = 0;
		virtual double GetDist(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint) const = 0;

		virtual double GetDistSquared(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint, bool& bOverlap) const = 0;
		virtual double GetDist(const PCGExData::FPoint& SourcePoint, const PCGExData::FPoint& TargetPoint, bool& bOverlap) const = 0;
	};

	PCGEXCORE_API const IDistances* GetDistances(
		const EPCGExDistance Source = EPCGExDistance::Center,
		const EPCGExDistance Target = EPCGExDistance::Center,
		const bool bOverlapIsZero = false,
		const EPCGExDistanceType Type = EPCGExDistanceType::Euclidian);

	PCGEXCORE_API const IDistances* GetNoneDistances();

	// Distances Initialization
	struct PCGEXCORE_API FDistancesStatic
	{
		TMap<TTuple<EPCGExDistance, EPCGExDistance, EPCGExDistanceType, bool>, TSharedPtr<IDistances>> Cache;
		FDistancesStatic();
	};

}
