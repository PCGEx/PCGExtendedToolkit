// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExOBB.h"
#include "Math/GenericOctree.h"

#include "PCGExOBBIntersections.generated.h"

UENUM()
enum class EPCGExCutType : uint8
{
	Undefined   = 0 UMETA(DisplayName = "Undefined"),
	Entry       = 1 UMETA(DisplayName = "Entry"),
	EntryNoExit = 2 UMETA(DisplayName = "Entry (no exit)"),
	Exit        = 3 UMETA(DisplayName = "Exit"),
	ExitNoEntry = 4 UMETA(DisplayName = "Exit (no entry)"),
};

namespace PCGExMath::OBB
{
	struct PCGEXCORE_API FCut
	{
		FVector Position = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		int32 BoxIndex = -1; // Point index
		int32 Idx = -1;      // Collection index/idx
		EPCGExCutType Type = EPCGExCutType::Undefined;

		FCut() = default;

		FCut(const FVector& InPos, const FVector& InNormal, int32 InBoxIdx, int32 InIdx, EPCGExCutType InType)
			: Position(InPos)
			  , Normal(InNormal)
			  , BoxIndex(InBoxIdx)
			  , Idx(InIdx)
			  , Type(InType)
		{
		}

		FORCEINLINE bool IsEntry() const
		{
			return Type == EPCGExCutType::Entry || Type == EPCGExCutType::EntryNoExit;
		}

		FORCEINLINE bool IsExit() const
		{
			return Type == EPCGExCutType::Exit || Type == EPCGExCutType::ExitNoEntry;
		}
	};

	// Intersection collection

	struct PCGEXCORE_API FIntersections
	{
		TArray<FCut> Cuts;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;

		FIntersections() = default;

		FIntersections(const FVector& InStart, const FVector& InEnd)
			: Start(InStart)
			  , End(InEnd)
		{
		}

		FORCEINLINE bool IsEmpty() const
		{
			return Cuts.IsEmpty();
		}

		FORCEINLINE int32 Num() const
		{
			return Cuts.Num();
		}

		void Reset(const FVector& InStart, const FVector& InEnd);

		void Add(const FVector& Pos, const FVector& Normal, int32 BoxIdx, int32 CloudIdx, EPCGExCutType Type)
		{
			Cuts.Emplace(Pos, Normal, BoxIdx, CloudIdx, Type);
		}

		void Sort();
		void SortAndDedupe();

		FBoxCenterAndExtent GetBounds() const
		{
			FBox Box(ForceInit);
			Box += Start;
			Box += End;
			return FBoxCenterAndExtent(Box);
		}
	};

	// Intersection functions

	/**
	 * Raw segment-box intersection. Returns hit points and normals.
	 */
	PCGEXCORE_API bool SegmentBoxRaw(
		const FOBB& Box, const FVector& Start, const FVector& End,
		FVector& OutHit1, FVector& OutHit2,
		FVector& OutNormal1, FVector& OutNormal2,
		bool& bOutHit2Valid,
		bool& bOutInverseDir);


	/**
	 * Process segment against single box, adding cuts to collection.
	 */
	PCGEXCORE_API bool ProcessSegment(const FOBB& Box, FIntersections& IO, int32 CloudIndex = -1);

	/**
	 * Quick segment-box test (no detailed results).
	 */
	PCGEXCORE_API bool SegmentIntersects(const FOBB& Box, const FVector& Start, const FVector& End);
}
