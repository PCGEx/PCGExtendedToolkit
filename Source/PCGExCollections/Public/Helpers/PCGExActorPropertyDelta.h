// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class AActor;

namespace PCGExActorDelta
{
	/**
	 * Serialize properties that differ from defaults for an actor AND its components.
	 * Actor-level properties are diffed against the actor CDO.
	 * Each instanced component is diffed against its archetype.
	 * Returns empty array if actor and all components match defaults exactly.
	 *
	 * Format is opaque — use ApplyPropertyDelta to deserialize.
	 */
	PCGEXCOLLECTIONS_API TArray<uint8> SerializeActorDelta(AActor* Actor);

	/**
	 * Apply a previously serialized property delta to an actor and its components.
	 * Components are matched by name. Missing/renamed components are safely skipped.
	 */
	PCGEXCOLLECTIONS_API void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes);

	/** Compute CRC32 hash of delta bytes. Returns 0 for empty input. */
	PCGEXCOLLECTIONS_API uint32 HashDelta(const TArray<uint8>& DeltaBytes);
}
