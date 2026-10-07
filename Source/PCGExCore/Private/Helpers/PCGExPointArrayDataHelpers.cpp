// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExPointArrayDataHelpers.h"

#include "CoreMinimal.h"

#include "PCGContext.h"
#include "Core/PCGExMTCommon.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGExArrayHelpers.h"

namespace PCGExPointArrayDataHelpers
{
	FReadWriteScope::FReadWriteScope(const int32 NumElements, const bool bSetNum)
	{
		if (bSetNum)
		{
			ReadIndices.SetNumUninitialized(NumElements);
			WriteIndices.SetNumUninitialized(NumElements);
		}
		else
		{
			ReadIndices.Reserve(NumElements);
			WriteIndices.Reserve(NumElements);
		}
	}

	int32 FReadWriteScope::Add(const int32 ReadIndex, const int32 WriteIndex)
	{
		ReadIndices.Add(ReadIndex);
		return WriteIndices.Add(WriteIndex);
	}

	int32 FReadWriteScope::Add(const TArrayView<int32> ReadIndicesRange, int32& OutWriteIndex)
	{
		for (const int32 ReadIndex : ReadIndicesRange)
		{
			Add(ReadIndex, OutWriteIndex++);
		}
		return ReadIndices.Num() - 1;
	}

	void FReadWriteScope::Set(const int32 Index, const int32 ReadIndex, const int32 WriteIndex)
	{
		ReadIndices[Index] = ReadIndex;
		WriteIndices[Index] = WriteIndex;
	}

	void FReadWriteScope::CopyPoints(const UPCGBasePointData* Read, UPCGBasePointData* Write, const bool bClean, const bool bInitializeMetadata)
	{
		if (bInitializeMetadata)
		{
			EPCGPointNativeProperties Properties = EPCGPointNativeProperties::All;
			EnumRemoveFlags(Properties, EPCGPointNativeProperties::MetadataEntry);

			Read->CopyPropertiesTo(Write, ReadIndices, WriteIndices, Properties);

			TPCGValueRange<int64> OutMetadataEntries = Write->GetMetadataEntryValueRange();
			for (const int32 WriteIndex : WriteIndices)
			{
				Write->Metadata->InitializeOnSet(OutMetadataEntries[WriteIndex]);
			}
		}
		else
		{
			Read->CopyPointsTo(Write, ReadIndices, WriteIndices);
		}

		if (bClean)
		{
			ReadIndices.Empty();
			WriteIndices.Empty();
		}
	}

	void FReadWriteScope::CopyProperties(const UPCGBasePointData* Read, UPCGBasePointData* Write, EPCGPointNativeProperties Properties, const bool bClean)
	{
		Read->CopyPropertiesTo(Write, ReadIndices, WriteIndices, Properties);
		if (bClean)
		{
			ReadIndices.Empty();
			WriteIndices.Empty();
		}
	}

	int32 SetNumPointsAllocated(UPCGBasePointData* InData, const int32 InNumPoints, const EPCGPointNativeProperties Properties)
	{
		InData->SetNumPoints(InNumPoints);
		InData->AllocateProperties(Properties);
		return InNumPoints;
	}

	template <typename T>
	void ReorderValueRange(TPCGValueRange<T>& InRange, const TArray<int32>& InOrder)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPointArrayDataHelpers::ReorderValueRange);

		const int32 NumIndices = InOrder.Num();
		TArray<T> ValuesCopy;
		PCGExArrayHelpers::InitArray(ValuesCopy, NumIndices);
		if (NumIndices < 4096)
		{
			for (int i = 0; i < NumIndices; i++)
			{
				ValuesCopy[i] = MoveTemp(InRange[InOrder[i]]);
			}
			for (int i = 0; i < NumIndices; i++)
			{
				InRange[i] = MoveTemp(ValuesCopy[i]);
			}
		}
		else
		{
			PCGEX_PARALLEL_FOR(NumIndices, ValuesCopy[i] = InRange[InOrder[i]];)
			PCGEX_PARALLEL_FOR(NumIndices, InRange[i] = ValuesCopy[i];)
		}
	}

#define PCGEX_TPL(_TYPE, _NAME, ...) \
template PCGEXCORE_API void ReorderValueRange<_TYPE>(TPCGValueRange<_TYPE>& InRange, const TArray<int32>& InOrder);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	void Reorder(UPCGBasePointData* InData, const TArray<int32>& InOrder)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExPointArrayDataHelpers::Reorder);

		EPCGPointNativeProperties AllocatedProperties = InData->GetAllocatedProperties();

#define PCGEX_REORDER_RANGE_DECL(_NAME, _TYPE, ...) \
	if(EnumHasAnyFlags(AllocatedProperties, EPCGPointNativeProperties::_NAME)){ \
		TPCGValueRange<_TYPE> Range = InData->Get##_NAME##ValueRange(true); \
		ReorderValueRange<_TYPE>(Range, InOrder);}

		PCGEX_FOREACH_POINT_NATIVE_PROPERTY(PCGEX_REORDER_RANGE_DECL)
#undef PCGEX_REORDER_RANGE_DECL
	}

	void PointsToPositions(const UPCGBasePointData* InPointData, TArray<FVector>& OutPositions)
	{
		const int32 NumPoints = InPointData->GetNumPoints();
		const TConstPCGValueRange<FTransform> Transforms = InPointData->GetConstTransformValueRange();
		PCGExArrayHelpers::InitArray(OutPositions, NumPoints);
		PCGEX_PARALLEL_FOR(NumPoints, OutPositions[i] = Transforms[i].GetLocation();)
	}

	void InitEmptyNativeProperties(const UPCGData* From, UPCGData* To, EPCGPointNativeProperties Properties)
	{
		const UPCGPointArrayData* FromPoints = Cast<UPCGPointArrayData>(From);
		UPCGPointArrayData* ToPoints = Cast<UPCGPointArrayData>(To);

		if (!FromPoints || !ToPoints || FromPoints == ToPoints)
		{
			return;
		}

		ToPoints->CopyUnallocatedPropertiesFrom(FromPoints);
		ToPoints->AllocateProperties(FromPoints->GetAllocatedProperties());
	}

	EPCGPointNativeProperties GetPointNativeProperties(uint8 Flags)
	{
		const EPCGExPointNativeProperties InFlags = static_cast<EPCGExPointNativeProperties>(Flags);
		EPCGPointNativeProperties OutFlags = EPCGPointNativeProperties::None;

		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::Transform))
		{
			OutFlags |= EPCGPointNativeProperties::Transform;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::Density))
		{
			OutFlags |= EPCGPointNativeProperties::Density;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::BoundsMin))
		{
			OutFlags |= EPCGPointNativeProperties::BoundsMin;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::BoundsMax))
		{
			OutFlags |= EPCGPointNativeProperties::BoundsMax;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::Color))
		{
			OutFlags |= EPCGPointNativeProperties::Color;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::Steepness))
		{
			OutFlags |= EPCGPointNativeProperties::Steepness;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::Seed))
		{
			OutFlags |= EPCGPointNativeProperties::Seed;
		}
		if (EnumHasAnyFlags(InFlags, EPCGExPointNativeProperties::MetadataEntry))
		{
			OutFlags |= EPCGPointNativeProperties::MetadataEntry;
		}

		return OutFlags;
	}
}
