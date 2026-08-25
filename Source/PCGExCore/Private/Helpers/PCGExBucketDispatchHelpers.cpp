// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExBucketDispatchHelpers.h"

#include "PCGParamData.h"
#include "Containers/PCGExScopedContainers.h"
#include "Core/PCGExContext.h"
#include "Data/PCGExPointIO.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Metadata/PCGMetadata.h"

namespace PCGExBucketDispatchHelpers
{
	void DispatchBuckets(
		TConstArrayView<TSharedPtr<PCGExData::FPointIOCollection>> Buckets,
		const TSharedPtr<PCGExData::FPointIOCollection>& UnmatchedBucket,
		TConstArrayView<int32> Counts,
		TConstArrayView<TSharedPtr<PCGExMT::TScopedArray<int32>>> ScopedIndices,
		int32 NumPoints,
		FCreateIOFn CreateIO)
	{
		const int32 NumBuckets = Buckets.Num();
		const int32 UnmatchedIdx = NumBuckets;

		check(Counts.Num() == NumBuckets + 1);
		check(ScopedIndices.Num() == NumBuckets + 1);

		// Single-bucket zero-copy optimization: if every point landed in one bucket, Forward it.
		int32 SingleBucket = -1;
		for (int32 i = 0; i <= UnmatchedIdx; i++)
		{
			if (Counts[i] == NumPoints)
			{
				SingleBucket = i;
				break;
			}
		}

		if (SingleBucket >= 0)
		{
			if (SingleBucket == UnmatchedIdx)
			{
				if (UnmatchedBucket)
				{
					(void)CreateIO(UnmatchedBucket.ToSharedRef(), PCGExData::EIOInit::Forward);
				}
			}
			else
			{
				(void)CreateIO(Buckets[SingleBucket].ToSharedRef(), PCGExData::EIOInit::Forward);
			}
			return;
		}

		// Mixed distribution: create a new output per non-empty named bucket.
		for (int32 i = 0; i < NumBuckets; i++)
		{
			if (Counts[i] <= 0)
			{
				continue;
			}

			TArray<int32> ReadIndices;
			ScopedIndices[i]->Collapse(ReadIndices);

			TSharedPtr<PCGExData::FPointIO> BucketIO = CreateIO(Buckets[i].ToSharedRef(), PCGExData::EIOInit::New);
			if (!BucketIO)
			{
				continue;
			}

			PCGExPointArrayDataHelpers::SetNumPointsAllocated(BucketIO->GetOut(), ReadIndices.Num(), BucketIO->GetAllocations());
			BucketIO->InheritProperties(ReadIndices, BucketIO->GetAllocations());
		}

		// Unmatched bucket (only when the caller opted in by passing a non-null collection).
		if (UnmatchedBucket && Counts[UnmatchedIdx] > 0)
		{
			TArray<int32> ReadIndices;
			ScopedIndices[UnmatchedIdx]->Collapse(ReadIndices);

			TSharedPtr<PCGExData::FPointIO> UnmatchedIO = CreateIO(UnmatchedBucket.ToSharedRef(), PCGExData::EIOInit::New);
			if (!UnmatchedIO)
			{
				return;
			}

			PCGExPointArrayDataHelpers::SetNumPointsAllocated(UnmatchedIO->GetOut(), ReadIndices.Num(), UnmatchedIO->GetAllocations());
			UnmatchedIO->InheritProperties(ReadIndices, UnmatchedIO->GetAllocations());
		}
	}

	UPCGParamData* ExtractParamRows(FPCGExContext* InContext, const UPCGParamData* InSource, TConstArrayView<int32> InRows)
	{
		UPCGParamData* NewParamData = InContext->ManagedObjects->New<UPCGParamData>();
		if (!NewParamData)
		{
			return nullptr;
		}

		// Row indices are entry keys: the temp conversion maps entry i -> point i (see ToPointData).
		TArray<PCGMetadataEntryKey> EntriesToCopy;
		EntriesToCopy.Reserve(InRows.Num());
		for (const int32 Row : InRows)
		{
			EntriesToCopy.Add(Row);
		}

		NewParamData->Metadata->InitializeAsCopy(FPCGMetadataInitializeParams(InSource->Metadata, &EntriesToCopy));
		return NewParamData;
	}

	void DispatchParamBuckets(
		FPCGExContext* InContext,
		const UPCGParamData* InSourceParam,
		TConstArrayView<TSharedPtr<PCGExData::FPointIOCollection>> Buckets,
		const TSharedPtr<PCGExData::FPointIOCollection>& UnmatchedBucket,
		TConstArrayView<int32> Counts,
		TConstArrayView<TSharedPtr<PCGExMT::TScopedArray<int32>>> ScopedIndices,
		int32 NumRows,
		FCreateRawIOFn CreateIO)
	{
		const int32 NumBuckets = Buckets.Num();
		const int32 UnmatchedIdx = NumBuckets;

		check(Counts.Num() == NumBuckets + 1);
		check(ScopedIndices.Num() == NumBuckets + 1);

		// Single-bucket zero-copy optimization: forward the original param data untouched.
		int32 SingleBucket = -1;
		for (int32 i = 0; i <= UnmatchedIdx; i++)
		{
			if (Counts[i] == NumRows)
			{
				SingleBucket = i;
				break;
			}
		}

		if (SingleBucket >= 0)
		{
			const TSharedPtr<PCGExData::FPointIOCollection>& Target = SingleBucket == UnmatchedIdx ? UnmatchedBucket : Buckets[SingleBucket];
			if (Target)
			{
				if (const TSharedPtr<PCGExData::FPointIO> IO = CreateIO(Target.ToSharedRef()))
				{
					IO->SetOutputOverride(const_cast<UPCGParamData*>(InSourceParam), false);
				}
			}
			return;
		}

		auto EmitBucket = [&](const TSharedRef<PCGExData::FPointIOCollection>& InCollection, const int32 BucketIdx)
		{
			TArray<int32> ReadIndices;
			ScopedIndices[BucketIdx]->Collapse(ReadIndices);

			const TSharedPtr<PCGExData::FPointIO> BucketIO = CreateIO(InCollection);
			if (!BucketIO)
			{
				return;
			}

			if (UPCGParamData* BucketParam = ExtractParamRows(InContext, InSourceParam, ReadIndices))
			{
				BucketIO->SetOutputOverride(BucketParam, true);
			}
		};

		for (int32 i = 0; i < NumBuckets; i++)
		{
			if (Counts[i] > 0)
			{
				EmitBucket(Buckets[i].ToSharedRef(), i);
			}
		}

		if (UnmatchedBucket && Counts[UnmatchedIdx] > 0)
		{
			EmitBucket(UnmatchedBucket.ToSharedRef(), UnmatchedIdx);
		}
	}
}
