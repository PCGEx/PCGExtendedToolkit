// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOctree.h"
#include "Helpers/PCGExTargetsHandler.h"

namespace PCGExMatching
{
	/**
	 * Per-target-point spatial index for range-gated sampling where the range lives on the target.
	 * Each item is the target point's spatialized bounds (per the target EPCGExDistance mode) expanded by that
	 * point's own max range, so a source point falls within a target's range only if it intersects that item.
	 * The resolved per-point ranges are kept so the sample loop reads two doubles instead of re-evaluating getters.
	 * Composes over FTargetsHandler: the handler's own octrees are the engine's and cannot carry a per-point inflation.
	 */
	class PCGEXMATCHING_API FTargetsRangeIndex
	{
	protected:
		struct FEntry
		{
			TUniquePtr<PCGExOctree::FItemOctree> Octree;
			FBox Bounds = FBox(ForceInit);
			TArray<double> MinRanges; // per point, clamped >= 0 and ordered against MaxRanges
			TArray<double> MaxRanges;
		};

		TSharedRef<FTargetsHandler> Handler;
		TArray<FEntry> Entries;
		TUniquePtr<PCGExOctree::FItemOctree> DataOctree;

	public:
		/** RangeAt(PointIndex, OutMin, OutMax) yields the raw pair; the index clamps and orders it. */
		using FRangeAt = TFunctionRef<void(int32, double&, double&)>;

		explicit FTargetsRangeIndex(const TSharedRef<FTargetsHandler>& InHandler);

		/** Builds one target's octree and range arrays; safe to call concurrently for distinct IOs (writes only Entries[IO]). */
		void BuildTarget(const int32 IO, const EPCGExDistance TargetDistanceMode, FRangeAt RangeAt);

		/** Builds the outer per-data octree from every entry's bounds. Call once, single-threaded, after all BuildTarget calls. */
		void BuildDataOctree();

		FORCEINLINE void GetRange(const int32 IO, const int32 Index, double& OutMin, double& OutMax) const
		{
			const FEntry& Entry = Entries[IO];
			OutMin = Entry.MinRanges[Index];
			OutMax = Entry.MaxRanges[Index];
		}

		/** Same contract as FTargetsHandler::ForEachElementWithBoundsTest, over the range-inflated items. */
		template <typename Func>
		void ForEachElementWithBoundsTest(const FBoxCenterAndExtent& QueryBounds, Func&& InFunc, const TSet<const UPCGData*>* Exclude = nullptr) const
		{
			if (!DataOctree)
			{
				return;
			}

			DataOctree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& DataItem)
			{
				const UPCGBasePointData* Data = Handler->GetData(DataItem.Index);
				if (Exclude && Exclude->Contains(Data))
				{
					return;
				}

				const FEntry& Entry = Entries[DataItem.Index];
				check(Entry.Octree)

				Entry.Octree->FindElementsWithBoundsTest(QueryBounds, [&](const PCGExOctree::FItem& PointItem)
				{
					InFunc(PCGExData::FConstPoint(Data, PointItem.Index, DataItem.Index));
				});
			});
		}

		/** World-space box the IDistances mode measures from; conservative for every EPCGExDistanceType. */
		static FBox GetSpatializedBox(const PCGExData::FConstPoint& Point, const EPCGExDistance Mode);
	};
}
