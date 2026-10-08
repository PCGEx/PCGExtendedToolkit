// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "Details/PCGExActorSelectionDetails.h"

#include "PCGExActorBounds.generated.h"

class AActor;
class UPCGBasePointData;
struct FPCGExContext;

UENUM()
enum class EPCGExActorBoundsSource : uint8
{
	ActorSpace      = 0 UMETA(DisplayName = "Actor Space", Tooltip = "Point takes the actor transform; bounds are the cached component world boxes brought into actor space. Loose on rotated actors (two AABB inflations), but never touches component internals."),
	WorldAABB       = 1 UMETA(DisplayName = "World AABB", Tooltip = "Point sits at the center of the world-space box with identity rotation and unit scale."),
	ActorSpaceLocal = 2 UMETA(DisplayName = "Actor Space (Local Bounds)", Tooltip = "Point takes the actor transform; each component's own local bounds are carried into actor space in one step, non-uniform scale included (one AABB inflation only, exact for unrotated components). Components whose local bounds disagree with their world bounds, and unloaded World Partition actors, behave as Actor Space."),
	PerPrimitive    = 3 UMETA(DisplayName = "Per Primitive", Tooltip = "One point per primitive component: the component transform and its own local bounds, the tightest representation available. Point count is no longer one per actor. Components whose local bounds disagree with their world bounds get their world box brought into component space; unloaded World Partition actors behave as Actor Space."),
};

/** How the per-actor point is shaped. Only primitives that collide or are visible in game contribute. Reads cached
 *  component bounds, never geometry; the Local Bounds and Per Primitive modes also ask each component for its own
 *  local box, kept only when it agrees with the cached world bounds. */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSSPATIAL_API FPCGExActorBoundsOutputDetails
{
	GENERATED_BODY()

	/** Which frame the point transform and bounds are expressed in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExActorBoundsSource BoundsSource = EPCGExActorBoundsSource::ActorSpace;

	/** Skip primitives PCG spawned (tagged with the PCG generated component tag). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnorePCGGeneratedComponents = true;

	/** Skip editor-only primitives, and every primitive of an editor-only actor. Turn off to account for
	 *  editor-only markers; they still need collision or in-game visibility to contribute. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnoreEditorOnly = true;

	/** Also gather primitives owned by child actor components. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIncludeChildActors = true;

	/** Omit actors with no primitive bounds instead of emitting a zero-extent point at their location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOmitActorsWithoutBounds = true;

	/** False when BoundsSource holds a value no mode handles, e.g. an enumerator that no longer resolves. Check before snapshotting. */
	bool IsUsable() const;
};

namespace PCGExActorBounds
{
	/** Everything a point needs, captured on the game thread; plain data so the write can run anywhere. */
	struct FSnapshot
	{
		FTransform Transform = FTransform::Identity;
		FBox LocalBounds = FBox(ForceInit);
	};

	/**
	 * The cull box of one execution. For AABBs, overlapping both the input and self is overlapping their
	 * intersection, so a single box carries both constraints. Disjoint input and self: nothing can match.
	 */
	struct FCull
	{
		FBox InputBox = FBox(ForceInit);
		FBox Box = FBox(ForceInit);
		bool bDisjoint = false;

		const FBox* Get() const
		{
			return Box.IsValid ? &Box : nullptr;
		}
	};

	/**
	 * Resolves the cull box from the Bounds pin (when bounded) and the execution source (when overlapping self).
	 * Returns false after logging when the node should output nothing.
	 */
	PCGEXELEMENTSSPATIAL_API bool ResolveCull(FPCGExContext* InContext, FName InBoundsPin, bool bUnbounded, bool bMustOverlapSelf, FCull& OutCull);

#if WITH_EDITOR
	/** Static keys for the authored selection; skipped when the Bounds pin is wired, tracking is dynamic then. */
	PCGEXELEMENTSSPATIAL_API void AddStaticTrackedKeys(const UPCGSettings* InOwner, const FPCGExActorSelectionDetails& InSelection, FName InBoundsPin, FPCGSelectionKeyToSettingsMap& OutKeysToSettings);

	/** Dynamic keys when the selection is overridden or a Bounds input exists. Stock semantics: an input box rides on the key, a self-only cull marks it culled. */
	PCGEXELEMENTSSPATIAL_API void RegisterDynamicTracking(FPCGExContext* InContext, const FPCGExActorSelectionDetails& InSelection, const FCull& InCull, FName InSelectionProperty);
#endif

	/**
	 * Appends the snapshot(s) of a live actor to OutSnapshots: one per actor, or one per primitive in Per Primitive mode.
	 * Culling against InCullBox (null = no cull) and the no-bounds omission use each primitive's cached world Bounds.
	 * Returns the number of snapshots appended.
	 */
	PCGEXELEMENTSSPATIAL_API int32 SnapshotActor(const AActor* InActor, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, TArray<FSnapshot>& OutSnapshots);

	/** Appends the snapshot of a transform and a single world-space box (e.g. a World Partition descriptor). Same return contract as SnapshotActor. */
	PCGEXELEMENTSSPATIAL_API int32 SnapshotBox(const FTransform& InActorTransform, const FBox& InWorldBounds, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, TArray<FSnapshot>& OutSnapshots);

	/**
	 * One game-thread sweep: the resolved selection and cull, its tag matcher, and the snapshot lists actors are routed to.
	 * InSelection must have been Init()'d and must outlive the sweep. The class filter and the scope are the caller's
	 * to apply: the sweep only tests tags, and only when the selection's criteria apply.
	 */
	struct FSweep
	{
		PCGEXELEMENTSSPATIAL_API FSweep(const FPCGExActorSelectionDetails& InSelection, const FPCGExActorBoundsOutputDetails& InOutput, TArray<FSnapshot>& InKept);

		const FPCGExActorSelectionDetails& Selection;
		const FPCGExActorBoundsOutputDetails& Output;
		TArray<FSnapshot>& Kept;

		/** Where actors carrying an Exclude tag go; null drops them before their bounds are read. */
		TArray<FSnapshot>* Discarded = nullptr;
		const FBox* CullBox = nullptr;

		/** Skipped with its child actors; null skips nothing. */
		const AActor* Self = nullptr;

		PCGExActorSelection::FTagMatcher Tags;

		/** Snapshots a live actor if its tags pass. */
		PCGEXELEMENTSSPATIAL_API void AddActor(const AActor* InActor);

		/** Same for an actor only known by its tags, transform and world box, e.g. an unloaded World Partition actor. */
		PCGEXELEMENTSSPATIAL_API void AddBox(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds);

	private:
		TArray<FSnapshot>* Route(const TArray<FName>& InActorTags);
	};

	/**
	 * One allocation, then straight range writes; no metadata entries are created. Points are written in an order
	 * derived from their own values, so the output never depends on the order snapshots were gathered in.
	 */
	PCGEXELEMENTSSPATIAL_API void WritePoints(UPCGBasePointData* InData, const TArray<FSnapshot>& InSnapshots);
}
