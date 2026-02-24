// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class AActor;

namespace PCGExActorDelta
{
	/** Serialize only properties that differ from the CDO (UE tagged property format).
	 *  Returns empty array if actor matches CDO exactly. */
	PCGEXCOLLECTIONS_API TArray<uint8> SerializeActorDelta(AActor* Actor);

	/** Apply a previously serialized property delta to an actor.
	 *  Unknown/removed properties are auto-skipped via size tags — safe across class versions. */
	PCGEXCOLLECTIONS_API void ApplyPropertyDelta(AActor* Actor, const TArray<uint8>& DeltaBytes);

	/** Compute CRC32 hash of delta bytes. Returns 0 for empty input. */
	PCGEXCOLLECTIONS_API uint32 HashDelta(const TArray<uint8>& DeltaBytes);
}
