// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Math/PCGExMathBounds.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGExData.h"
#include "Utils/PCGValueRange.h"

namespace PCGExMath
{
	FBox GetLocalBounds(const PCGExData::FConstPoint& Point, const EPCGExPointBoundsSource Source)
	{
		switch (Source)
		{
		case EPCGExPointBoundsSource::ScaledBounds:
			return GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(Point);
		case EPCGExPointBoundsSource::DensityBounds:
			return GetLocalBounds<EPCGExPointBoundsSource::DensityBounds>(Point);
		case EPCGExPointBoundsSource::Bounds:
			return GetLocalBounds<EPCGExPointBoundsSource::Bounds>(Point);
		case EPCGExPointBoundsSource::Center:
			return GetLocalBounds<EPCGExPointBoundsSource::Center>(Point);
		default:
			return FBox(FVector::OneVector * -1, FVector::OneVector);
		}
	}

	FBox GetLocalBounds(const PCGExData::FProxyPoint& Point, const EPCGExPointBoundsSource Source)
	{
		switch (Source)
		{
		case EPCGExPointBoundsSource::ScaledBounds:
			return GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(Point);
		case EPCGExPointBoundsSource::DensityBounds:
			return GetLocalBounds<EPCGExPointBoundsSource::DensityBounds>(Point);
		case EPCGExPointBoundsSource::Bounds:
			return GetLocalBounds<EPCGExPointBoundsSource::Bounds>(Point);
		case EPCGExPointBoundsSource::Center:
			return GetLocalBounds<EPCGExPointBoundsSource::Center>(Point);
		default:
			return FBox(FVector::OneVector * -1, FVector::OneVector);
		}
	}

	FBox GetBounds(const TArrayView<FVector> InPositions)
	{
		FBox Bounds = FBox(ForceInit);
		for (const FVector& Position : InPositions)
		{
			Bounds += Position;
		}
		SanitizeBounds(Bounds);
		return Bounds;
	}

	FBox GetBounds(const TConstPCGValueRange<FTransform>& InTransforms)
	{
		FBox Bounds = FBox(ForceInit);
		for (const FTransform& Transform : InTransforms)
		{
			Bounds += Transform.GetLocation();
		}
		SanitizeBounds(Bounds);
		return Bounds;
	}

	FBox GetBounds(const UPCGBasePointData* InPointData, const EPCGExPointBoundsSource Source)
	{
		FBox Bounds = FBox(ForceInit);
		const int32 NumPoints = InPointData->GetNumPoints();
		FTransform Transform;

		switch (Source)
		{
		case EPCGExPointBoundsSource::ScaledBounds:
			for (int i = 0; i < NumPoints; i++)
			{
				PCGExData::FConstPoint Point(InPointData, i);
				Point.GetTransformNoScale(Transform);
				Bounds += PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::ScaledBounds>(Point).TransformBy(Transform);
			}
			break;
		case EPCGExPointBoundsSource::DensityBounds:
			for (int i = 0; i < NumPoints; i++)
			{
				PCGExData::FConstPoint Point(InPointData, i);
				Point.GetTransformNoScale(Transform);
				Bounds += PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::DensityBounds>(Point).TransformBy(Transform);
			}
			break;
		case EPCGExPointBoundsSource::Bounds:
			for (int i = 0; i < NumPoints; i++)
			{
				PCGExData::FConstPoint Point(InPointData, i);
				Point.GetTransformNoScale(Transform);
				Bounds += PCGExMath::GetLocalBounds<EPCGExPointBoundsSource::Bounds>(Point).TransformBy(Transform);
			}
			break;
		default: case EPCGExPointBoundsSource::Center:
			for (int i = 0; i < NumPoints; i++)
			{
				Bounds += PCGExData::FConstPoint(InPointData, i).GetLocation();
			}
			break;
		}

		SanitizeBounds(Bounds);
		return Bounds;
	}
}
