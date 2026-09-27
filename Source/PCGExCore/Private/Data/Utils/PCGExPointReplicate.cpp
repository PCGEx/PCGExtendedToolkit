// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Data/Utils/PCGExPointReplicate.h"

#include "PCGExLog.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGBasePointData.h"
#include "Data/Utils/PCGExDataForward.h"
#include "Fitting/PCGExFittingCommon.h"
#include "Helpers/PCGExRandomHelpers.h"
#include "Metadata/PCGMetadata.h"

namespace PCGExPointReplicate
{
	template <typename T>
	void CopyTiled(const TConstPCGValueRange<T>& InRange, TPCGValueRange<T>& OutRange, const int32 NumSourcePoints, const PCGExMT::FScope& Scope)
	{
		int32 i = Scope.Start % NumSourcePoints;
		for (int32 o = Scope.Start; o < Scope.End; o++)
		{
			OutRange[o] = InRange[i];
			if (++i == NumSourcePoints) { i = 0; }
		}
	}

	bool Replicate(const UPCGBasePointData* Source, UPCGBasePointData* Out, const FCopies& Copies, const FForward* Forward)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPointReplicate::Replicate);

		check(Source);
		check(Out);

		const int32 NumCopies = Copies.Transforms.Num();
		const int32 NumSourcePoints = Source->GetNumPoints();
		const int64 NumOutPoints64 = static_cast<int64>(NumCopies) * NumSourcePoints;

		if (NumOutPoints64 > MAX_int32)
		{
			UE_LOG(LogPCGEx, Error, TEXT("Point replication aborted: %d copies of %d points exceed the point count limit."), NumCopies, NumSourcePoints);
			return false;
		}

		const int32 NumOutPoints = static_cast<int32>(NumOutPoints64);
		const bool bForward = Forward && Forward->Handler && !Forward->Handler->IsEmpty();
		check(!bForward || Forward->SourceIndices.Num() == NumCopies);

		// Legacy point data reports every property as allocated, so it is copied per point.
		const EPCGPointNativeProperties SourceAllocations = Source->GetAllocatedProperties();
		EPCGPointNativeProperties Allocations = SourceAllocations | EPCGPointNativeProperties::Transform;
		if (Copies.bRefreshSeeds) { EnumAddFlags(Allocations, EPCGPointNativeProperties::Seed); }
		if (bForward) { EnumAddFlags(Allocations, EPCGPointNativeProperties::MetadataEntry); }

		Out->SetNumPoints(NumOutPoints, /*bInitializeValues=*/false);
		Out->AllocateProperties(Allocations);

		if (NumOutPoints == 0)
		{
			return true;
		}

		// Fetched once and non-allocating: every range written below was allocated above, and chunks never overlap.
		const TConstPCGValueRange<FTransform> InTransforms = Source->GetConstTransformValueRange();
		TPCGValueRange<FTransform> OutTransforms = Out->GetTransformValueRange(false);

#define PCGEX_REPLICATE_RANGES(_NAME) \
		const bool bCopy##_NAME = EnumHasAnyFlags(Allocations, EPCGPointNativeProperties::_NAME); \
		const auto In##_NAME = Source->GetConst##_NAME##ValueRange(); \
		auto Out##_NAME = Out->Get##_NAME##ValueRange(false);

		PCGEX_REPLICATE_RANGES(Density)
		PCGEX_REPLICATE_RANGES(BoundsMin)
		PCGEX_REPLICATE_RANGES(BoundsMax)
		PCGEX_REPLICATE_RANGES(Color)
		PCGEX_REPLICATE_RANGES(Steepness)
		PCGEX_REPLICATE_RANGES(Seed)
		PCGEX_REPLICATE_RANGES(MetadataEntry)

#undef PCGEX_REPLICATE_RANGES

		PCGExMT::ParallelOrSequentialScoped(
			NumOutPoints, [&](const PCGExMT::FScope& Scope)
			{
				PCGExFitting::DispatchInheritStrategy(
					Copies.InheritStrategy, [&](auto StrategyTag)
					{
						using FStrategy = decltype(StrategyTag);
						int32 CopyIndex = Scope.Start / NumSourcePoints;
						int32 i = Scope.Start - CopyIndex * NumSourcePoints;
						for (int32 o = Scope.Start; o < Scope.End; o++)
						{
							FTransform& Transform = OutTransforms[o];
							Transform = InTransforms[i];
							PCGExFitting::ApplyInheritedTransform<FStrategy::Value>(Transform, Copies.Transforms[CopyIndex]);
							if (++i == NumSourcePoints)
							{
								i = 0;
								CopyIndex++;
							}
						}
					});

				if (bCopyDensity) { CopyTiled(InDensity, OutDensity, NumSourcePoints, Scope); }
				if (bCopyBoundsMin) { CopyTiled(InBoundsMin, OutBoundsMin, NumSourcePoints, Scope); }
				if (bCopyBoundsMax) { CopyTiled(InBoundsMax, OutBoundsMax, NumSourcePoints, Scope); }
				if (bCopyColor) { CopyTiled(InColor, OutColor, NumSourcePoints, Scope); }
				if (bCopySteepness) { CopyTiled(InSteepness, OutSteepness, NumSourcePoints, Scope); }
				if (bCopyMetadataEntry) { CopyTiled(InMetadataEntry, OutMetadataEntry, NumSourcePoints, Scope); }

				if (Copies.bRefreshSeeds)
				{
					for (int32 o = Scope.Start; o < Scope.End; o++) { OutSeed[o] = PCGExRandomHelpers::ComputeSpatialSeed(OutTransforms[o].GetLocation()); }
				}
				else if (bCopySeed)
				{
					CopyTiled(InSeed, OutSeed, NumSourcePoints, Scope);
				}
			});

		if (!bForward)
		{
			// Tiled source keys resolve through the parent metadata: nothing else to write.
			return true;
		}

		// Per-copy values need entries of their own; AddEntriesInPlace keeps each source key as the new entry's parent.
		TArray<int64*> EntryKeys;
		EntryKeys.SetNumUninitialized(NumOutPoints);
		for (int32 o = 0; o < NumOutPoints; o++) { EntryKeys[o] = &OutMetadataEntry[o]; }
		Out->Metadata->AddEntriesInPlace(EntryKeys);

		Forward->Handler->ForwardToCopies(Forward->SourceIndices, Out, NumSourcePoints);
		return true;
	}
}
