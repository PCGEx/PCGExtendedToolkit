// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExCommon.h"
#include "Containers/PCGExIndexLookup.h"
#include "Data/PCGExPointElements.h"
#include "Math/PCGExMathDistances.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/Object.h"

namespace PCGExMath
{
	class IDistances;
}

namespace PCGEx
{
	class FIndexLookup;
}

class UPCGBasePointData;

namespace PCGExDetails
{
	class FDistances;
}

namespace PCGExData
{
	struct FConstPoint;
	struct FWeightedPoint;
	struct FPoint;
	struct FElement;

#pragma region Compound

	// Single chokepoint for inverse-distance weight computation over any iterable of FElement.
	// Both IUnionData (TSet-backed) and FUnionTable (CSR span) call into this.
	// Templated rather than coerced through a common iterator type because TSet's storage isn't
	// contiguous -- can't expose it as an array view without copying.
	template <typename TElementIterable>
	int32 ComputeUnionWeights(
		const TElementIterable& InElements,
		const TArray<const UPCGBasePointData*>& Sources,
		const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
		const FPoint& Target,
		const PCGExMath::IDistances* InDistances,
		TArray<FWeightedPoint>& OutWeightedPoints)
	{
		OutWeightedPoints.Reset(InElements.Num());

		double MaxWeight = 0;
		int32 Index = 0;

		for (const FElement& Element : InElements)
		{
			// Abstract elements (IO < 0) have no source; skip before the unchecked Get(-1) reads OOB.
			if (Element.IO < 0)
			{
				continue;
			}

			const int32 IOIdx = IdxLookup->Get(Element.IO);
			if (IOIdx == -1)
			{
				continue;
			}

			FWeightedPoint& P = OutWeightedPoints.Emplace_GetRef(Element.Index, 0, IOIdx);
			const double Dist = InDistances->GetDistSquared(FConstPoint(Sources[P.IO], P), Target);
			P.Weight = Dist;

			MaxWeight = FMath::Max(MaxWeight, Dist);
			Index++;
		}

		if (Index == 0)
		{
			return 0;
		}

		// A zero max distance has nothing to normalize by: the total stays 0 and the equal split below applies.
		double TotalWeight = 0;
		if (MaxWeight != 0)
		{
			for (FWeightedPoint& P : OutWeightedPoints)
			{
				TotalWeight += (P.Weight = 1 - (P.Weight / MaxWeight));
			}
		}

		if (Index == 1)
		{
			OutWeightedPoints[0].Weight = 1;
			return 1;
		}

		if (TotalWeight == 0)
		{
			const double StaticWeight = 1 / static_cast<double>(Index);
			for (FWeightedPoint& P : OutWeightedPoints)
			{
				P.Weight = StaticWeight;
			}
			return Index;
		}

		return Index;
	}

	// The elements ComputeUnionWeights keeps, in the same order, with no distance read: every weight is left at 1.
	// Only for a consumer that reads no weight.
	template <typename TElementIterable>
	int32 CollectUnionElements(
		const TElementIterable& InElements,
		const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
		TArray<FWeightedPoint>& OutWeightedPoints)
	{
		OutWeightedPoints.Reset(InElements.Num());

		for (const FElement& Element : InElements)
		{
			// Abstract elements (IO < 0) have no source; skip before the unchecked Get(-1) reads OOB.
			if (Element.IO < 0)
			{
				continue;
			}

			const int32 IOIdx = IdxLookup->Get(Element.IO);
			if (IOIdx == -1)
			{
				continue;
			}

			OutWeightedPoints.Emplace(Element.Index, 1.0, IOIdx);
		}

		return OutWeightedPoints.Num();
	}

	// Read interface for any container of fused source-element groups, regardless of underlying storage.
	// Implemented by the legacy sparse FUnionMetadata (TSet-of-IUnionData per entry) and the dense
	// FUnionTable (CSR layout). FGraph stores TSharedPtr<IUnionMetadata>; consumers that need the
	// rich legacy write API (NewEntryAt_Unsafe, etc.) hold concrete FUnionMetadata locally.
	class PCGEXBLENDING_API IUnionMetadata
	{
	public:
		virtual ~IUnionMetadata() = default;

		virtual int32 Num() const = 0;
		virtual int32 Size(int32 EntryIndex) const = 0;

		FORCEINLINE bool IsUnion(const int32 EntryIndex) const
		{
			return Size(EntryIndex) > 1;
		}

		// Output is bit-identical across impls (both route through ComputeUnionWeights).
		virtual int32 ComputeWeights(
			int32 EntryIndex,
			const TArray<const UPCGBasePointData*>& Sources,
			const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
			const FPoint& Target,
			const PCGExMath::IDistances* InDistances,
			TArray<FWeightedPoint>& OutWeightedPoints) const = 0;

		virtual bool ContainsIO(int32 EntryIndex, int32 IO) const = 0;
		virtual TSet<int32> GetIOSet(int32 EntryIndex) const = 0;
		virtual bool IOIndexOverlap(int32 EntryIndex, const TSet<int32>& InIndices) const = 0;
	};

	class PCGEXBLENDING_API IUnionData : public TSharedFromThis<IUnionData>
	{
	protected:
		mutable FRWLock UnionLock;

	public:
		TSet<FElement, DefaultKeyFuncs<FElement>, InlineSparseAllocator> Elements;

		IUnionData() = default;
		virtual ~IUnionData() = default;

		FORCEINLINE int32 Num() const
		{
			return Elements.Num();
		}

		FORCEINLINE void Add_Unsafe(const FElement& Point)
		{
			Elements.Add(FElement(Point.Index == -1 ? 0 : Point.Index, Point.IO));
		}

		void Add(const FElement& Point);

		FORCEINLINE void Add_Unsafe(const int32 Index, const int32 IO)
		{
			Elements.Add(FElement(Index == -1 ? 0 : Index, IO));
		}

		void Add(const int32 Index, const int32 IO);

		FORCEINLINE TSet<int32> GetIOSet() const
		{
			TSet<int32> Result;
			for (const FElement& E : Elements)
			{
				Result.Add(E.IO);
			}
			return Result;
		}

		FORCEINLINE bool ContainsIO(const int32 IO) const
		{
			for (const FElement& E : Elements)
			{
				if (E.IO == IO)
				{
					return true;
				}
			}
			return false;
		}

		void Add_Unsafe(const int32 IOIndex, const TArray<int32>& PointIndices);
		void Add(const int32 IOIndex, const TArray<int32>& PointIndices);

		bool IsEmpty() const
		{
			return Elements.IsEmpty();
		}

		virtual int32 ComputeWeights(
			const TArray<const UPCGBasePointData*>& Sources,
			const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
			const FPoint& Target,
			const PCGExMath::IDistances* InDistances,
			TArray<FWeightedPoint>& OutWeightedPoints) const;

		virtual void Reserve(const int32 InSetReserve, const int32 InElementReserve);
		virtual void Reset();
	};

	class PCGEXBLENDING_API FUnionMetadata : public IUnionMetadata, public TSharedFromThis<FUnionMetadata>
	{
	public:
		TArray<TSharedPtr<IUnionData>> Entries;

		FUnionMetadata() = default;
		virtual ~FUnionMetadata() override = default;

		// IUnionMetadata
		virtual int32 Num() const override
		{
			return Entries.Num();
		}

		virtual int32 Size(int32 EntryIndex) const override;
		virtual int32 ComputeWeights(
			int32 EntryIndex,
			const TArray<const UPCGBasePointData*>& Sources,
			const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
			const FPoint& Target,
			const PCGExMath::IDistances* InDistances,
			TArray<FWeightedPoint>& OutWeightedPoints) const override;
		virtual bool ContainsIO(int32 EntryIndex, int32 IO) const override;
		virtual TSet<int32> GetIOSet(int32 EntryIndex) const override;
		virtual bool IOIndexOverlap(int32 EntryIndex, const TSet<int32>& InIndices) const override;

		void SetNum(const int32 InNum);

		TSharedPtr<IUnionData> NewEntryAt_Unsafe(const int32 ItemIndex);

		FORCEINLINE void Append_Unsafe(const int32 Index, const FPoint& Point)
		{
			Entries[Index]->Add_Unsafe(Point);
		}

		FORCEINLINE void Append(const int32 Index, const FPoint& Point)
		{
			Entries[Index]->Add(Point);
		}

		FORCEINLINE TSharedPtr<IUnionData> Get(const int32 Index) const
		{
			return Entries.IsValidIndex(Index) ? Entries[Index] : nullptr;
		}
	};

#pragma endregion
}
