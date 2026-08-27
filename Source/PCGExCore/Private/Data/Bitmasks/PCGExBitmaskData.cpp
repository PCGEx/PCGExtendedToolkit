// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Bitmasks/PCGExBitmaskData.h"

#include "Data/Bitmasks/PCGExBitmaskCollection.h"
#include "Math/PCGExMath.h"

namespace PCGExBitmask
{
	void FBitmaskData::Append(const UPCGExBitmaskCollection* InCollection, const double InAngle, EPCGExBitOp Op)
	{
		const int32 Reserve = Directions.Num() + InCollection->NumEntries();

		Bitmasks.Reserve(Reserve);
		Directions.Reserve(Reserve);
		Dots.Reserve(Reserve);

		const double Dot = PCGExMath::DegreesToDot(InAngle);

		for (const FPCGExBitmaskCollectionEntry& Entry : InCollection->Entries)
		{
			FPCGExSimpleBitmask& Bitmask = Bitmasks.Emplace_GetRef();
			Bitmask.Bitmask = Entry.Bitmask.Get();
			Bitmask.Op = Op;
			Directions.Add(Entry.GetDirection());
			Dots.Add(Dot);
		}
	}

	void FBitmaskData::Append(const FPCGExBitmaskRef& InBitmaskRef, const double InAngle)
	{
		FPCGExSimpleBitmask& Bitmask = Bitmasks.Emplace_GetRef();
		FVector& Dir = Directions.Emplace_GetRef();

		if (!InBitmaskRef.TryGetAdjacencyInfos(Dir, Bitmask))
		{
			Bitmasks.Pop();
			Directions.Pop();
			return;
		}

		Dots.Add(PCGExMath::DegreesToDot(InAngle));
	}

	void FBitmaskData::Append(const TArray<FPCGExBitmaskRef>& InBitmaskRef, const double InAngle)
	{
		const int32 Reserve = Directions.Num() + InBitmaskRef.Num();

		Bitmasks.Reserve(Reserve);
		Directions.Reserve(Reserve);
		Dots.Reserve(Reserve);

		for (const FPCGExBitmaskRef& Ref : InBitmaskRef)
		{
			Append(Ref, InAngle);
		}
	}

	void FBitmaskData::MutateMatch(const FVector& InDirection, int64& Flags) const
	{
		const int32 NumEntries = Directions.Num();
		for (int i = 0; i < NumEntries; i++)
		{
			if (FVector::DotProduct(InDirection, Directions[i]) >= Dots[i])
			{
				Bitmasks[i].Mutate(Flags);
			}
		}
	}

	void FBitmaskData::MutateUnmatch(const FVector& InDirection, int64& Flags) const
	{
		const int32 NumEntries = Directions.Num();
		for (int i = 0; i < NumEntries; i++)
		{
			if (FVector::DotProduct(InDirection, Directions[i]) <= Dots[i])
			{
				Bitmasks[i].Mutate(Flags);
			}
		}
	}

	TSharedPtr<FBitmaskData> FBitmaskData::Make(const TMap<TObjectPtr<UPCGExBitmaskCollection>, EPCGExBitOp_OR>& InCollections, const TArray<FPCGExBitmaskRef>& InReferences, const double Angle)
	{
		TSharedPtr<FBitmaskData> Data = MakeShared<FBitmaskData>();
		Data->Append(InReferences, Angle);
		for (const TPair<TObjectPtr<UPCGExBitmaskCollection>, EPCGExBitOp_OR>& Pair : InCollections)
		{
			if (Pair.Key)
			{
				Data->Append(Pair.Key, Angle, GetBitOp(Pair.Value));
			}
		}
		return Data;
	}
}
