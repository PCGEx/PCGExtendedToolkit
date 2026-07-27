// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/


#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

namespace PCGExHelpers
{
	/**
	 * Whether UWorld::SpawnActor is safe to call on InWorld right now.
	 *
	 * SpawnActor asserts hard if the world is mid-transition, and the callers that need a throwaway
	 * actor are reached from paths that don't guarantee a settled world (manual rebuilds, deferred
	 * PostLoad, asset-updated-on-disk callbacks, editor-mode refreshes, recursive cascades from the
	 * level exporter). IsAsyncLoading is intentionally NOT part of this test: it is globally true
	 * during PCG graph execution that soft-loads assets, so testing it would silently strip results
	 * from every such run. Always false while the loader is routing PostLoad: spawning runs
	 * construction scripts, and ProcessEvent hard-asserts in that window.
	 */
	PCGEXCORE_API bool IsSpawnSafe(const UWorld* InWorld);

	/**
	 * Spawns a throwaway actor of InClass at identity in InWorld, hands it to Body, then hides it,
	 * disables its collision and destroys it. Collision handling is AlwaysSpawn, so the actor never
	 * relocates itself and Body sees component transforms relative to the origin.
	 *
	 * This is how the toolkit measures anything that only exists on a real instance -- bounds,
	 * SCS-added components, per-instance data -- none of which is reachable from the CDO.
	 *
	 * Returns false without running Body when InWorld is null, IsSpawnSafe fails, or the spawn
	 * itself fails. Callers own the messaging: nothing here logs, so each module reports the miss
	 * in its own idiom.
	 */
	PCGEXCORE_API bool WithTempActor(UWorld* InWorld, UClass* InClass, TFunctionRef<void(AActor*)> Body);
}
