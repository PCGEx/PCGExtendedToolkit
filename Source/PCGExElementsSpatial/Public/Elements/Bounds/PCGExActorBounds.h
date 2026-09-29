// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "Templates/SubclassOf.h"

#include "PCGExActorBounds.generated.h"

class AActor;
class UPCGBasePointData;
struct FPCGCrc;
struct FPCGExContext;
struct FPCGGetDependenciesCrcParams;
struct FPCGSelectionKey;

UENUM()
enum class EPCGExActorSelection : uint8
{
	ByClass = 0 UMETA(DisplayName = "By Class", Tooltip = "Select actors of the given class, subclasses included."),
	ByTag   = 1 UMETA(DisplayName = "By Tag", Tooltip = "Select actors carrying the given tag(s)."),
};

UENUM()
enum class EPCGExActorTagMatch : uint8
{
	Any = 0 UMETA(DisplayName = "Any", Tooltip = "An actor matches when it carries at least one of the tags."),
	All = 1 UMETA(DisplayName = "All", Tooltip = "An actor matches only when it carries every tag."),
};

UENUM()
enum class EPCGExActorBoundsSource : uint8
{
	ActorSpace      = 0 UMETA(DisplayName = "Actor Space", Tooltip = "Point takes the actor transform; bounds are the cached component world boxes brought into actor space. Loose on rotated actors (two AABB inflations), but never touches component internals."),
	WorldAABB       = 1 UMETA(DisplayName = "World AABB", Tooltip = "Point sits at the center of the world-space box with identity rotation and unit scale."),
	ActorSpaceLocal = 2 UMETA(DisplayName = "Actor Space (Local Bounds)", Tooltip = "Point takes the actor transform; each component's own local bounds are carried into actor space in one step, non-uniform scale included (one AABB inflation only, exact for unrotated components). Components whose local bounds disagree with their world bounds, and unloaded World Partition actors, behave as Actor Space."),
	PerPrimitive    = 3 UMETA(DisplayName = "Per Primitive", Tooltip = "One point per primitive component: the component transform and its own local bounds, the tightest representation available. Point count is no longer one per actor. Components whose local bounds disagree with their world bounds get their world box brought into component space; unloaded World Partition actors behave as Actor Space."),
};

/** A tag list split once into exact names and wildcard patterns. Exact tags are FName compares; only patterns pay a string match. */
struct PCGEXELEMENTSSPATIAL_API FPCGExActorTagSet
{
	TArray<FName> Exact;
	TArray<FString> Wildcards;

	void Init(const TArray<FName>& InTags, bool bAllowWildcards);

	bool IsEmpty() const
	{
		return Exact.IsEmpty() && Wildcards.IsEmpty();
	}

	/** True when the actor carries at least one tag of the set. */
	bool MatchesAny(const TArray<FName>& InActorTags) const;

	/** True when the actor carries every tag of the set. */
	bool MatchesAll(const TArray<FName>& InActorTags) const;
};

/** Class-or-tag actor selection, plus an "any of" skip list applied after it. Exact FName tag matching unless wildcards are opted in. */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSSPATIAL_API FPCGExActorSelectionDetails
{
	GENERATED_BODY()

	FPCGExActorSelectionDetails();

	/** How actors are selected. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExActorSelection Selection = EPCGExActorSelection::ByClass;

	/** Actor class to select; subclasses match. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, AllowAbstract = "true", EditCondition = "Selection == EPCGExActorSelection::ByClass", EditConditionHides))
	TSubclassOf<AActor> ActorClass;

	/** Tags to test against each actor's tags. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Selection == EPCGExActorSelection::ByTag", EditConditionHides))
	TArray<FName> Tags;

	/** Whether an actor needs any or all of the tags. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Selection == EPCGExActorSelection::ByTag", EditConditionHides))
	EPCGExActorTagMatch TagMatch = EPCGExActorTagMatch::Any;

	/** Treat '*' and '?' in tags (selection and skip lists alike) as wildcards. Only tags that actually contain one pay a string match per actor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bAllowWildcards = false;

	/** Actors that passed the selection above but carry ANY of these tags are skipped. Tested before the actor's bounds are read,
	 *  so a skipped actor costs no more than a non-matching one unless the node outputs its Discarded pin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	TArray<FName> SkipTags;

	/** Skip the actor that owns the executing PCG component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Skip actors spawned by PCG (tagged "PCG Generated Actor"), so generated content never feeds back into
	 *  its own inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnorePCGSpawnedActors = true;

	/** Splits selection and skip tags into exact and wildcard lists once. Call before matching. */
	void Init();

	/** False when nothing can match: null class, or no tags. */
	bool IsUsable() const;

	/** Class the world iteration is restricted to: ActorClass when selecting by class, AActor otherwise. */
	TSubclassOf<AActor> GetIterationClass() const;

	bool MatchesClass(const AActor* InActor) const;

	/** Tag selection plus the PCG-spawned exclusion; true for any tag list when selecting by class. */
	bool MatchesTags(const TArray<FName>& InActorTags) const;

	/** True when a skip list exists; hoist it out of the sweep so actors pay nothing when it is empty. */
	bool HasSkipTags() const
	{
		return !Skip.IsEmpty();
	}

	/** True when the actor carries any of the skip tags. Only meaningful after MatchesTags passed. */
	bool ShouldSkip(const TArray<FName>& InActorTags) const
	{
		return Skip.MatchesAny(InActorTags);
	}

	/** One PCG tracking key per class or tag, so edits to matching actors refresh the graph. Skip tags need no key of their
	 *  own: a skipped actor already matches the selection, so it is already tracked. */
	void MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const;

	/** Node subtitle: the class name, or the tag list; followed by the skip list when any. */
	FString GetTitleInformation() const;

private:
	FPCGExActorTagSet Select;
	FPCGExActorTagSet Skip;
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

	/** Self bounds enter the cache key only when they drive the cull; the Bounds input is CRC'd as input data. */
	PCGEXELEMENTSSPATIAL_API void CombineSelfBoundsCrc(const FPCGGetDependenciesCrcParams& InParams, bool bMustOverlapSelf, FPCGCrc& InOutCrc);

#if WITH_EDITOR
	/** Static keys for the authored selection; skipped when the Bounds pin is wired, tracking is dynamic then. */
	PCGEXELEMENTSSPATIAL_API void AddStaticTrackedKeys(const UPCGSettings* InOwner, const FPCGExActorSelectionDetails& InSelection, FName InBoundsPin, bool bMustOverlapSelf, FPCGSelectionKeyToSettingsMap& OutKeysToSettings);

	/** Dynamic keys when the selection is overridden or a Bounds input exists. Stock semantics: an input box rides on the key, a self-only cull marks it culled. */
	PCGEXELEMENTSSPATIAL_API void RegisterDynamicTracking(FPCGExContext* InContext, const FPCGExActorSelectionDetails& InSelection, const FCull& InCull, bool bMustOverlapSelf, FName InSelectionProperty);
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
	 * One game-thread sweep: the resolved selection and cull, and the snapshot lists actors are routed to.
	 * Holds references only; InSelection must have been Init()'d and must outlive the sweep.
	 */
	struct FSweep
	{
		PCGEXELEMENTSSPATIAL_API FSweep(const FPCGExActorSelectionDetails& InSelection, const FPCGExActorBoundsOutputDetails& InOutput, TArray<FSnapshot>& InKept);

		const FPCGExActorSelectionDetails& Selection;
		const FPCGExActorBoundsOutputDetails& Output;
		TArray<FSnapshot>& Kept;

		/** Where actors carrying a skip tag go; null drops them before their bounds are read. */
		TArray<FSnapshot>* Discarded = nullptr;
		const FBox* CullBox = nullptr;
		const AActor* Self = nullptr;

		/** Snapshots a live actor that passes the selection. */
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
