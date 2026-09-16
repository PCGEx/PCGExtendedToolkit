// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "Math/RandomStream.h"

#include "PCGExRandomHelpers.generated.h"

class UPCGComponent;
class UPCGSettings;

UENUM(meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true", DisplayName="[PCGEx] Seed Components"))
enum class EPCGExSeedComponents : uint8
{
	None      = 0 UMETA(Hidden),
	Local     = 1 << 1 UMETA(DisplayName = "Local"),
	Settings  = 1 << 2 UMETA(DisplayName = "Settings"),
	Component = 1 << 3 UMETA(DisplayName = "Component"),
};

ENUM_CLASS_FLAGS(EPCGExSeedComponents)
using EPCGExSeedComponentsBitmask = TEnumAsByte<EPCGExSeedComponents>;

namespace PCGExRandomHelpers
{
	FORCEINLINE static double FastRand01(uint32& Seed)
	{
		Seed = Seed * 1664525u + 1013904223u;
		return (Seed & 0x00FFFFFF) / static_cast<double>(0x01000000);
	}

	PCGEXCORE_API int32 GetSeed(const int32 BaseSeed, const uint8 Flags, const int32 Local, const UPCGSettings* Settings = nullptr, const UPCGComponent* Component = nullptr);

	PCGEXCORE_API int32 GetSeed(const int32 BaseSeed, const int32 Local, const UPCGSettings* Settings = nullptr, const UPCGComponent* Component = nullptr);

	PCGEXCORE_API FRandomStream GetRandomStreamFromPoint(const int32 BaseSeed, const int32 Offset, const UPCGSettings* Settings = nullptr, const UPCGComponent* Component = nullptr);

	/**
	 * Computes a deterministic seed from a spatial position.
	 * Note: Uses integer-based hashing internally (via PCGHelpers::ComputeSeedFromPosition),
	 * so positions within ~1 unit of each other may produce identical seeds.
	 */
	PCGEXCORE_API int ComputeSpatialSeed(const FVector& Origin, const FVector& Offset = FVector::ZeroVector);

	/**
	 * Roll a uniform value in [0, Total) and return the first index k where Cumulative[k] > Roll, so a
	 * zero-weight bucket is never picked. Cumulative must be monotone non-decreasing (weights >= 0).
	 *
	 * @return INDEX_NONE for empty / non-positive-total inputs; otherwise an index in [0, Cumulative.Num()).
	 *         The last entry is returned only on numerical drift (Roll just past Cumulative.Last()).
	 */
	FORCEINLINE int32 RollCumulativeWeighted(TArrayView<const double> Cumulative, const double Total, const int32 Seed)
	{
		if (Cumulative.IsEmpty() || Total <= 0.0)
		{
			return INDEX_NONE;
		}
		const double Roll = FRandomStream(Seed).FRandRange(0.0, Total);
		return FMath::Min(Algo::UpperBound(Cumulative, Roll), Cumulative.Num() - 1);
	}

	/**
	 * Streaming variant: caller provides N and a weight getter (callable taking int32 -> double).
	 * Saves an allocation when weights are already addressable by index.
	 *
	 * @return INDEX_NONE for empty / non-positive-total inputs; otherwise k in [0, N).
	 */
	template <typename WeightFn>
	FORCEINLINE int32 RollWeightedStreaming(const int32 N, WeightFn&& GetWeight, const double Total, const int32 Seed)
	{
		if (N <= 0 || Total <= 0.0)
		{
			return INDEX_NONE;
		}
		const double Roll = FRandomStream(Seed).FRandRange(0.0, Total);
		double Acc = 0.0;
		for (int32 k = 0; k < N; ++k)
		{
			Acc += GetWeight(k);
			if (Roll < Acc)
			{
				return k;
			}
		}
		return N - 1;
	}
}
