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
	ActorSpace = 0 UMETA(DisplayName = "Actor Space", Tooltip = "Point takes the actor transform; bounds are the cached component boxes brought into actor space."),
	WorldAABB  = 1 UMETA(DisplayName = "World AABB", Tooltip = "Point sits at the center of the world-space box with identity rotation and unit scale."),
};

/** Class-or-tag actor selection. Exact FName tag matching unless wildcards are opted in. */
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

	/** Treat '*' and '?' in tags as wildcards. Only tags that actually contain one pay a string match per actor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Selection == EPCGExActorSelection::ByTag", EditConditionHides))
	bool bAllowWildcards = false;

	/** Skip the actor that owns the executing PCG component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Skip actors spawned by PCG (tagged "PCG Generated Actor"), so generated content never feeds back into
	 *  its own inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnorePCGSpawnedActors = true;

	/** Splits tags into exact and wildcard lists once. Call before matching. */
	void Init();

	/** False when nothing can match: null class, or no tags. */
	bool IsUsable() const;

	/** Class the world iteration is restricted to: ActorClass when selecting by class, AActor otherwise. */
	TSubclassOf<AActor> GetIterationClass() const;

	bool MatchesClass(const AActor* InActor) const;

	/** Tag selection plus the PCG-spawned exclusion; true for any tag list when selecting by class. */
	bool MatchesTags(const TArray<FName>& InActorTags) const;

	/** One PCG tracking key per class or tag, so edits to matching actors refresh the graph. */
	void MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const;

	/** Node subtitle: the class name, or the tag list. */
	FString GetTitleInformation() const;

private:
	TArray<FName> ExactTags;
	TArray<FString> WildcardTags;
};

/** How the per-actor point is shaped. Always reads cached component bounds, never recomputes them. */
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

	/** Also gather primitives owned by child actor components. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIncludeChildActors = true;

	/** Omit actors with no primitive bounds instead of emitting a zero-extent point at their location. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bOmitActorsWithoutBounds = true;
};

namespace PCGExActorBounds
{
	/** Everything a point needs, captured on the game thread; plain data so the write can run anywhere. */
	struct FSnapshot
	{
		FTransform Transform = FTransform::Identity;
		FBox LocalBounds = FBox(ForceInit);
		FName SortKey = NAME_None;
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
	 * Snapshot from a live actor using each primitive's cached world Bounds.
	 * Returns false when the actor misses InCullBox (null = no cull), or has no bounds and omission is on.
	 */
	PCGEXELEMENTSSPATIAL_API bool SnapshotActor(const AActor* InActor, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, FSnapshot& OutSnapshot);

	/** Snapshot from a transform and a single world-space box (e.g. a World Partition descriptor). Same return contract as SnapshotActor. */
	PCGEXELEMENTSSPATIAL_API bool SnapshotBox(const FTransform& InActorTransform, const FBox& InWorldBounds, const FName InSortKey, const FPCGExActorBoundsOutputDetails& InDetails, const FBox* InCullBox, FSnapshot& OutSnapshot);

	/** Lexical sort on SortKey so the output CRC does not depend on level actor-array order. */
	PCGEXELEMENTSSPATIAL_API void Sort(TArray<FSnapshot>& InOutSnapshots);

	/** One allocation, then straight range writes. No metadata entries are created. */
	PCGEXELEMENTSSPATIAL_API void WritePoints(UPCGBasePointData* InData, const TArray<FSnapshot>& InSnapshots);
}
