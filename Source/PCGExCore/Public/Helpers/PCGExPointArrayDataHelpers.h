// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"
#include "PCGExMetaHelpersMacros.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Utils/PCGValueRange.h"

#include "PCGExPointArrayDataHelpers.generated.h"

class UPCGBasePointData;
class UPCGManagedComponent;
class UPCGData;
class UPCGComponent;

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] Native Point Properties"))
enum class EPCGExPointNativeProperties : uint8
{
	None          = 0,
	Transform     = 1 << 0 UMETA(DisplayName = "Transform"),
	Density       = 1 << 1 UMETA(DisplayName = "Density"),
	BoundsMin     = 1 << 2 UMETA(DisplayName = "BoundsMin"),
	BoundsMax     = 1 << 3 UMETA(DisplayName = "BoundsMax"),
	Color         = 1 << 4 UMETA(DisplayName = "Color"),
	Steepness     = 1 << 5 UMETA(DisplayName = "Steepness"),
	Seed          = 1 << 6 UMETA(DisplayName = "Seed"),
	MetadataEntry = 1 << 7 UMETA(DisplayName = "MetadataEntry"),
};

ENUM_CLASS_FLAGS(EPCGExPointNativeProperties)
using EPCGExNativePointPropertiesBitmask = TEnumAsByte<EPCGExPointNativeProperties>;

namespace PCGExPointArrayDataHelpers
{
	class PCGEXCORE_API FReadWriteScope
	{
		TArray<int32> ReadIndices;
		TArray<int32> WriteIndices;

	public:
		FReadWriteScope(const int32 NumElements, const bool bSetNum);

		int32 Add(const int32 ReadIndex, const int32 WriteIndex);
		int32 Add(const TArrayView<int32> ReadIndicesRange, int32& OutWriteIndex);
		void Set(const int32 Index, const int32 ReadIndex, const int32 WriteIndex);

		void CopyPoints(const UPCGBasePointData* Read, UPCGBasePointData* Write, const bool bClean = true, const bool bInitializeMetadata = false);
		void CopyProperties(const UPCGBasePointData* Read, UPCGBasePointData* Write, EPCGPointNativeProperties Properties, const bool bClean = true);

		bool IsEmpty() const
		{
			return ReadIndices.IsEmpty();
		}

		int32 Num() const
		{
			return ReadIndices.Num();
		}

		const TArray<int32>& GetReadIndices() const
		{
			return ReadIndices;
		}

		const TArray<int32>& GetWriteIndices() const
		{
			return WriteIndices;
		}
	};

	PCGEXCORE_API int32 SetNumPointsAllocated(UPCGBasePointData* InData, const int32 InNumPoints, EPCGPointNativeProperties Properties = EPCGPointNativeProperties::All);

	PCGEXCORE_API bool EnsureMinNumPoints(UPCGBasePointData* InData, const int32 InNumPoints);

	PCGEXCORE_API void InitEmptyNativeProperties(const UPCGData* From, UPCGData* To, EPCGPointNativeProperties Properties = EPCGPointNativeProperties::All);

	PCGEXCORE_API EPCGPointNativeProperties GetPointNativeProperties(uint8 Flags);

	template <typename T>
	static void Reverse(TPCGValueRange<T> Range)
	{
		const int32 Count = Range.Num();
		for (int32 i = 0; i < Count / 2; ++i)
		{
			Swap(Range[i], Range[Count - 1 - i]);
		}
	}

	template <typename T>
	void ReorderValueRange(TPCGValueRange<T>& InRange, const TArray<int32>& InOrder);

#define PCGEX_TPL(_TYPE, _NAME, ...) \
extern template void ReorderValueRange<_TYPE>(TPCGValueRange<_TYPE>& InRange, const TArray<int32>& InOrder);

	PCGEX_FOREACH_SUPPORTEDTYPES(PCGEX_TPL)
#undef PCGEX_TPL

	PCGEXCORE_API void Reorder(UPCGBasePointData* InData, const TArray<int32>& InOrder);

	PCGEXCORE_API void PointsToPositions(const UPCGBasePointData* InPointData, TArray<FVector>& OutPositions);
}
