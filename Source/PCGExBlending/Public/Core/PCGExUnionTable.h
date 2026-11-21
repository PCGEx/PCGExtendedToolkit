// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExUnionData.h"
#include "Data/PCGExPointElements.h"

namespace PCGExMath
{
	class IDistances;
}

namespace PCGEx
{
	class FIndexLookup;
}

class UPCGBasePointData;

namespace PCGExData
{
	struct FPoint;
	struct FWeightedPoint;
	struct FElement;

	// POD payload for the streaming union build.
	// Key is the spatial bucket key; (IO, Index) identify the source point.
	struct PCGEXBLENDING_API FUnionStreamRecord
	{
		uint64 Key = 0;
		int32 IO = -1;
		int32 Index = -1;

		FUnionStreamRecord() = default;

		FUnionStreamRecord(const uint64 InKey, const int32 InIO, const int32 InIndex)
			: Key(InKey)
			  , IO(InIO)
			  , Index(InIndex)
		{
		}
	};

	// Immutable, packed CSR result of a streaming union build.
	// One entry per unique Key; Get(i) returns a contiguous view of the source elements that mapped to it.
	class PCGEXBLENDING_API FUnionTable final : public IUnionMetadata
	{
	public:
		// Offsets[NumEntries+1] -- entry i covers Elements[Offsets[i] .. Offsets[i+1])
		TArray<int32> Offsets;
		TArray<FElement> Elements;
		TArray<uint64> Keys;

		FUnionTable() = default;
		virtual ~FUnionTable() override = default;

		// IUnionMetadata
		virtual int32 Num() const override
		{
			return Offsets.Num() > 0 ? Offsets.Num() - 1 : 0;
		}

		virtual int32 Size(const int32 EntryIndex) const override
		{
			return Offsets[EntryIndex + 1] - Offsets[EntryIndex];
		}

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

		// FUnionTable-specific (not on interface)
		FORCEINLINE uint64 GetKey(const int32 EntryIndex) const
		{
			return Keys[EntryIndex];
		}

		FORCEINLINE TConstArrayView<FElement> Get(const int32 EntryIndex) const
		{
			return MakeArrayView(Elements.GetData() + Offsets[EntryIndex], Size(EntryIndex));
		}

		static int32 ComputeWeightsForSpan(
			TConstArrayView<FElement> InElements,
			const TArray<const UPCGBasePointData*>& Sources,
			const TSharedPtr<PCGEx::FIndexLookup>& IdxLookup,
			const FPoint& Target,
			const PCGExMath::IDistances* InDistances,
			TArray<FWeightedPoint>& OutWeightedPoints);

		void Reset();
	};

	// Builds an FUnionTable by collecting per-scope records in parallel, then sorting and grouping.
	// Determinism: result depends only on (Key, ScopeIndex, in-scope position) of each emitted record.
	// Stable LSD radix sort preserves relative order within equal keys, so the output is bit-identical
	// across worker counts as long as scope partitioning is.
	class PCGEXBLENDING_API FUnionTableBuilder
	{
	public:
		FUnionTableBuilder() = default;

		explicit FUnionTableBuilder(const int32 NumScopes)
		{
			Init(NumScopes);
		}

		~FUnionTableBuilder() = default;

		// When set, Compile() collapses records sharing an (IO, Index) within a key group, so Size()
		// counts distinct merged points instead of raw records. Enable for NODE tables (a shared vtx is
		// emitted once per incident edge); leave false for EDGE tables, whose record count is the number
		// of contributing segments.
		bool bDedupeElementsBySource = false;

		void Init(const int32 NumScopes)
		{
			ScopedRecords.SetNum(NumScopes);
		}

		FORCEINLINE int32 NumScopes() const
		{
			return ScopedRecords.Num();
		}

		FORCEINLINE TArray<FUnionStreamRecord>& GetScope(const int32 ScopeIndex)
		{
			return ScopedRecords[ScopeIndex];
		}

		FORCEINLINE const TArray<FUnionStreamRecord>& GetScope(const int32 ScopeIndex) const
		{
			return ScopedRecords[ScopeIndex];
		}

		FORCEINLINE void Reserve(const int32 ScopeIndex, const int32 ExpectedRecords)
		{
			ScopedRecords[ScopeIndex].Reserve(ExpectedRecords);
		}

		FORCEINLINE void Emit(const int32 ScopeIndex, const uint64 Key, const int32 IO, const int32 Index)
		{
			ScopedRecords[ScopeIndex].Emplace(Key, IO, Index);
		}

		void Compile(FUnionTable& OutTable);

		// As Compile, but additionally fills:
		//   OutScopeOffsets : prefix sum of scope sizes (size NumScopes+1)
		//   OutRecordToEntry: for the K-th record in the global concatenation (where
		//                     K = OutScopeOffsets[ScopeIndex] + RecordIndexInScope),
		//                     the final entry index that record landed in.
		// Use this when the caller needs to remap a downstream structure (e.g. cluster edges) from
		// per-source indices to entry indices.
		void CompileWithMapping(FUnionTable& OutTable, TArray<int32>& OutRecordToEntry, TArray<int32>& OutScopeOffsets);

	private:
		TArray<TArray<FUnionStreamRecord>> ScopedRecords;
	};
}
