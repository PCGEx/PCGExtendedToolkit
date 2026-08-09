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

	/**
	 * Make every scene component's ComponentToWorld and Bounds trustworthy on a world that may
	 * have been loaded as an ASSET (never initialized, components never registered).
	 *
	 * ComponentToWorld is transient: on an asset-loaded world it sits at identity, so
	 * GetActorTransform / GetComponentTransform / world-space instance transforms /
	 * GetComponentsBoundingBox all read garbage until registration recomputes them -- which for
	 * an asset world never happens. This walks the persistent level's actors and calls
	 * ConditionalUpdateComponentToWorld on every scene component: the engine recomputes from the
	 * SERIALIZED RelativeLocation/Rotation/Scale3D, parent-first and socket-aware, and refreshes
	 * Bounds along the way. No registration, no world init, no render/physics state -- and a
	 * no-op on worlds that are live in the editor (their update flags are already set).
	 *
	 * Call this before harvesting transforms/bounds from any world obtained via a plain asset
	 * load (e.g. LoadBlocking_AnyThread on a TSoftObjectPtr<UWorld>).
	 */
	PCGEXCORE_API void EnsureWorldTransformsCurrent(UWorld* InWorld);
}
