// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOctree.h"
#include "Data/PCGExPointElements.h"

struct FPCGExFuseDetails;

namespace PCGExData
{
	// Sequential, single-threaded find-or-insert spatial registry for tolerance-based union dedup.
	// Backed by PCGExOctree::FItemOctree.
	//
	// Pair with FUnionTableBuilder: emit FindOrInsert() result as the Builder Key. After Compile(),
	// table entries align 1:1 with reps (entry index == RepIndex).
	//
	// Mirrors the historical FUnionGraph::InsertPoint octree semantics:
	//   - tolerance check uses each rep's *original* Point (stable)
	//   - closest-rep tie-break uses each rep's *running* Center (drifts as points accumulate)
	//   - on match, the matching rep's running mean is updated
	//
	// Tolerance is symmetric (larger of the point's and the rep's). Founding is first-come: insert largest first.
	class PCGEXBLENDING_API FUnionRegistry
	{
	public:
		struct FRep
		{
			FConstPoint Point;
			FVector CenterAccum = FVector::ZeroVector;
			int32 FuseCount = 0;
			int32 RepIndex = -1;

			FORCEINLINE FVector GetCenter() const
			{
				return CenterAccum / static_cast<double>(FuseCount);
			}

			FORCEINLINE void Accumulate(const FVector& Position)
			{
				CenterAccum += Position;
				FuseCount++;
			}
		};

		explicit FUnionRegistry(const FBox& InBounds);
		~FUnionRegistry() = default;

		// Pure query. Returns RepIndex of the rep within tolerance whose running Center is closest
		// to the query origin, or INDEX_NONE if none.
		int32 Find(const FConstPoint& Point, const FPCGExFuseDetails& FuseDetails) const;

		// Inserts a new rep with auto-assigned RepIndex (== Num() before the call).
		// Caller is responsible for not double-inserting; use FindOrInsert when in doubt.
		int32 Insert(const FConstPoint& Point, const FPCGExFuseDetails& FuseDetails);

		// Combined find-then-insert. On match, accumulates Point's location into the matching rep's
		// running Center and returns its RepIndex. On miss, inserts a new rep and returns its index.
		int32 FindOrInsert(const FConstPoint& Point, const FPCGExFuseDetails& FuseDetails);

		FORCEINLINE int32 Num() const
		{
			return Reps.Num();
		}

		FORCEINLINE const FRep& Get(const int32 RepIndex) const
		{
			return Reps[RepIndex];
		}

		void Reserve(const int32 ExpectedNum)
		{
			Reps.Reserve(ExpectedNum);
		}

	private:
		TArray<FRep> Reps;
		TUniquePtr<PCGExOctree::FItemOctree> Octree;
	};
}
