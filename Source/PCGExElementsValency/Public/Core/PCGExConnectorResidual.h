// Copyright 2026 Timoth Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

/**
 * Per-point attribute tracking remaining spawn capacity per connector type.
 * Packed as int64 with 4-bit nibbles: 16 connectors x 4 bits = 64 bits.
 * Each nibble stores a residual count (0-15) for one connector type.
 *
 * Attribute name: PCGEx/V/ConnectorResidual/{LayerSuffix}
 */
namespace PCGExValency::ConnectorResidual
{
	/** Maximum number of connector types supported (16 x 4-bit = 64 bits) */
	constexpr int32 MAX_CONNECTOR_TYPES = 16;

	/** Maximum residual count per connector (4 bits = 0-15) */
	constexpr int32 MAX_RESIDUAL = 15;

	/** Sentinel for "no residual data" */
	constexpr int64 INVALID_RESIDUAL = 0;

	/** Get the residual count for a connector type from packed int64 */
	FORCEINLINE int32 GetResidual(int64 Packed, int32 ConnectorIndex)
	{
		check(ConnectorIndex >= 0 && ConnectorIndex < MAX_CONNECTOR_TYPES);
		const int32 Shift = ConnectorIndex * 4;
		return static_cast<int32>((static_cast<uint64>(Packed) >> Shift) & 0xF);
	}

	/** Set the residual count for a connector type in packed int64 */
	FORCEINLINE int64 SetResidual(int64 Packed, int32 ConnectorIndex, int32 Count)
	{
		check(ConnectorIndex >= 0 && ConnectorIndex < MAX_CONNECTOR_TYPES);
		check(Count >= 0 && Count <= MAX_RESIDUAL);
		const int32 Shift = ConnectorIndex * 4;
		const uint64 Mask = ~(static_cast<uint64>(0xF) << Shift);
		const uint64 Value = static_cast<uint64>(Count) << Shift;
		return static_cast<int64>((static_cast<uint64>(Packed) & Mask) | Value);
	}

	/** Decrement the residual count for a connector type, clamped to 0 */
	FORCEINLINE int64 DecrementResidual(int64 Packed, int32 ConnectorIndex)
	{
		const int32 Current = GetResidual(Packed, ConnectorIndex);
		return Current > 0 ? SetResidual(Packed, ConnectorIndex, Current - 1) : Packed;
	}

	/** Check if a connector type has any remaining capacity */
	FORCEINLINE bool HasResidual(int64 Packed, int32 ConnectorIndex)
	{
		return GetResidual(Packed, ConnectorIndex) > 0;
	}

	/** Check if any connector type has remaining capacity */
	FORCEINLINE bool HasAnyResidual(int64 Packed)
	{
		return Packed != 0;
	}

	/**
	 * Build initial packed residual from an array of spawn capacities.
	 * Each capacity is clamped to [0, MAX_RESIDUAL].
	 * @param Capacities Array of spawn capacities indexed by connector type index
	 * @return Packed int64 with initial residual counts
	 */
	FORCEINLINE int64 PackCapacities(TConstArrayView<int32> Capacities)
	{
		int64 Packed = 0;
		const int32 Count = FMath::Min(Capacities.Num(), MAX_CONNECTOR_TYPES);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Clamped = FMath::Clamp(Capacities[i], 0, MAX_RESIDUAL);
			Packed = SetResidual(Packed, i, Clamped);
		}
		return Packed;
	}

	/** Get the attribute name for connector residuals on a layer */
	inline FName GetResidualAttributeName(const FName& LayerSuffix)
	{
		return FName(FString::Printf(TEXT("PCGEx/V/ConnectorResidual/%s"), *LayerSuffix.ToString()));
	}
}
