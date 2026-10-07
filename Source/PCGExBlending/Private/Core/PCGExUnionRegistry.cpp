// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExUnionRegistry.h"
#include "Data/PCGBasePointData.h"
#include "Details/PCGExFuseDetails.h"
#include "Math/PCGExMath.h"

namespace PCGExData
{
	FUnionRegistry::FUnionRegistry(const FBox& InBounds)
	{
		Octree = MakeUnique<PCGExOctree::FItemOctree>(InBounds.GetCenter(), InBounds.GetExtent().Length() + 10);
	}

	int32 FUnionRegistry::Find(const FConstPoint& Point, const FPCGExFuseDetails& FuseDetails) const
	{
		const FVector Origin = Point.GetLocation();
		PCGExMath::FClosestPosition Closest(Origin);

		Octree->FindElementsWithBoundsTest(FuseDetails.GetOctreeBox(Origin, Point.Index), [&](const PCGExOctree::FItem& Item)
		{
			const FRep& Rep = Reps[Item.Index];
			const bool bIsWithin = FuseDetails.bComponentWiseTolerance
				? FuseDetails.IsWithinToleranceComponentWise(Point, Rep.Point)
				: FuseDetails.IsWithinTolerance(Point, Rep.Point);

			if (bIsWithin)
			{
				Closest.Update(Rep.GetCenter(), Rep.RepIndex);
				return false;
			}
			return true;
		});

		return Closest.bValid ? Closest.Index : INDEX_NONE;
	}

	int32 FUnionRegistry::Insert(const FConstPoint& Point, const FPCGExFuseDetails& FuseDetails)
	{
		const int32 NewIndex = Reps.Num();
		const FVector Origin = Point.GetLocation();

		FRep& Rep = Reps.Emplace_GetRef();
		Rep.Point = Point;
		Rep.CenterAccum = Origin;
		Rep.FuseCount = 1;
		Rep.RepIndex = NewIndex;

		// Grown by the rep's own tolerance: Find's query box only carries the incoming point's.
		FBox Box = Point.Data->GetLocalBounds(Point.Index).TransformBy(Point.Data->GetTransform(Point.Index));
		Box += Origin;
		Box = Box.ExpandBy(FuseDetails.GetToleranceExtent(Point.Index));
		Octree->AddElement(PCGExOctree::FItem(NewIndex, FBoxSphereBounds(Box)));

		return NewIndex;
	}

	int32 FUnionRegistry::FindOrInsert(const FConstPoint& Point, const FPCGExFuseDetails& FuseDetails)
	{
		const int32 Existing = Find(Point, FuseDetails);
		if (Existing != INDEX_NONE)
		{
			Reps[Existing].Accumulate(Point.GetLocation());
			return Existing;
		}
		return Insert(Point, FuseDetails);
	}
}
