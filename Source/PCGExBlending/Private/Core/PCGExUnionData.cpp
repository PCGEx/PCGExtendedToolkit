// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExUnionData.h"

#include "Containers/PCGExIndexLookup.h"
#include "Data/PCGExData.h"
#include "Math/PCGExMathDistances.h"

namespace PCGExData
{
	void IUnionData::Add(const FElement& Point)
	{
		FWriteScopeLock WriteScopeLock(UnionLock);
		Add_Unsafe(Point.Index, Point.IO);
	}

	void IUnionData::Add(const int32 Index, const int32 IO)
	{
		FWriteScopeLock WriteScopeLock(UnionLock);
		Add_Unsafe(Index, IO);
	}

	void IUnionData::Add_Unsafe(const int32 IOIndex, const TArray<int32>& PointIndices)
	{
		Elements.Reserve(Elements.Num() + PointIndices.Num());
		for (const int32 A : PointIndices)
		{
			Elements.Add(FElement(A, IOIndex));
		}
	}

	void IUnionData::Add(const int32 IOIndex, const TArray<int32>& PointIndices)
	{
		FWriteScopeLock WriteScopeLock(UnionLock);
		Add_Unsafe(IOIndex, PointIndices);
	}

	int32 IUnionData::ComputeWeights(
		const TArray<const UPCGBasePointData*>& Sources,
		const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
		const FPoint& Target,
		const PCGExMath::IDistances* InDistances,
		TArray<FWeightedPoint>& OutWeightedPoints) const
	{
		return ComputeUnionWeights(Elements, Sources, IdxLookup, Target, InDistances, OutWeightedPoints);
	}

	void IUnionData::Reserve(const int32 InSetReserve, const int32 InElementReserve = 8)
	{
		if (InElementReserve > 8)
		{
			Elements.Reserve(InElementReserve);
		}
	}

	void IUnionData::Reset()
	{
		Elements.Reset();
	}

	int32 FUnionMetadata::Size(const int32 EntryIndex) const
	{
		if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex])
		{
			return 0;
		}
		return Entries[EntryIndex]->Num();
	}

	int32 FUnionMetadata::ComputeWeights(
		const int32 EntryIndex,
		const TArray<const UPCGBasePointData*>& Sources,
		const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
		const FPoint& Target,
		const PCGExMath::IDistances* InDistances,
		TArray<FWeightedPoint>& OutWeightedPoints) const
	{
		if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex])
		{
			return 0;
		}
		return Entries[EntryIndex]->ComputeWeights(Sources, IdxLookup, Target, InDistances, OutWeightedPoints);
	}

	bool FUnionMetadata::ContainsIO(const int32 EntryIndex, const int32 IO) const
	{
		if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex])
		{
			return false;
		}
		return Entries[EntryIndex]->ContainsIO(IO);
	}

	TSet<int32> FUnionMetadata::GetIOSet(const int32 EntryIndex) const
	{
		if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex])
		{
			return TSet<int32>();
		}
		return Entries[EntryIndex]->GetIOSet();
	}

	bool FUnionMetadata::IOIndexOverlap(const int32 EntryIndex, const TSet<int32>& InIndices) const
	{
		if (!Entries.IsValidIndex(EntryIndex) || !Entries[EntryIndex])
		{
			return false;
		}
		for (const FElement& E : Entries[EntryIndex]->Elements)
		{
			if (InIndices.Contains(E.IO))
			{
				return true;
			}
		}
		return false;
	}

	void FUnionMetadata::SetNum(const int32 InNum)
	{
		// To be used only with NewEntryAt / NewEntryAt_Unsafe
		Entries.Init(nullptr, InNum);
	}

	TSharedPtr<IUnionData> FUnionMetadata::NewEntryAt_Unsafe(const int32 ItemIndex)
	{
		Entries[ItemIndex] = MakeShared<IUnionData>();
		return Entries[ItemIndex];
	}
}
