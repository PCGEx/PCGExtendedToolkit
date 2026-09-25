// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
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

	/** Murmur3 fmix64. FRandomStream's single-step LCG maps nearby seeds to nearby first draws: mix low-entropy integers first. */
	FORCEINLINE uint64 Avalanche(uint64 Hash)
	{
		Hash ^= Hash >> 33;
		Hash *= 0xff51afd7ed558ccdULL;
		Hash ^= Hash >> 33;
		Hash *= 0xc4ceb9fe1a85ec53ULL;
		Hash ^= Hash >> 33;
		return Hash;
	}

	/** Well-spread seed for a sequential index; raw consecutive indices roll in sweeps. */
	FORCEINLINE int32 SeedFromIndex(const int32 Index)
	{
		const uint64 Hash = Avalanche(static_cast<uint32>(Index));
		return static_cast<int32>(static_cast<uint32>(Hash ^ (Hash >> 32)));
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
}
