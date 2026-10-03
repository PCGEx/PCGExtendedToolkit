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

/** Legacy: only read by the deprecated selection fields, to migrate them. */
UENUM()
enum class EPCGExActorSelection : uint8
{
	ByClass = 0 UMETA(DisplayName = "By Class", Tooltip = "Select actors of the given class, subclasses included."),
	ByTag   = 1 UMETA(DisplayName = "By Tag", Tooltip = "Select actors carrying the given tag(s)."),
};

/** Legacy: only read by the deprecated selection fields, to migrate them. */
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

/**
 * Which actors a sweep gathers: an optional class filter, then three comma-separated tag clauses matched as exact
 * names (case-insensitive) unless wildcards are opted in. Selects nothing without the class filter or a Require clause.
 */
USTRUCT(BlueprintType)
struct PCGEXELEMENTSSPATIAL_API FPCGExActorSelectionDetails
{
	GENERATED_BODY()

	FPCGExActorSelectionDetails();

	/** Restrict the sweep to one actor class. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, InlineEditConditionToggle))
	bool bFilterByClass = false;

	/** Actor class to gather; subclasses match. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, AllowAbstract = "true", EditCondition = "bFilterByClass"))
	TSubclassOf<AActor> ActorClass;

	/** Comma-separated tags an actor must all carry. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString RequireAll;

	/** Comma-separated tags an actor must carry at least one of. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString RequireAny;

	/** Comma-separated tags that exclude an otherwise selected actor. Tested before its bounds are read, so an excluded
	 *  actor costs no more than a non-matching one unless the node outputs its Discarded pin. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString Exclude;

	/** Treat '*' and '?' in any clause as wildcards. A pattern is string-matched once per distinct actor tag,
	 *  then remembered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bAllowWildcards = false;

	/** Skip the actor that owns the executing PCG component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnoreSelf = true;

	/** Skip actors spawned by PCG (tagged "PCG Generated Actor"), so generated content never feeds back into
	 *  its own inputs. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bIgnorePCGSpawnedActors = true;

#pragma region DEPRECATED

	UPROPERTY(meta = (DeprecatedProperty, ScriptNoExport))
	EPCGExActorSelection Selection_DEPRECATED = EPCGExActorSelection::ByClass;

	UPROPERTY(meta = (DeprecatedProperty, ScriptNoExport))
	TArray<FName> Tags_DEPRECATED;

	UPROPERTY(meta = (DeprecatedProperty, ScriptNoExport))
	EPCGExActorTagMatch TagMatch_DEPRECATED = EPCGExActorTagMatch::Any;

	UPROPERTY(meta = (DeprecatedProperty, ScriptNoExport))
	TArray<FName> SkipTags_DEPRECATED;

#pragma endregion

#if WITH_EDITOR
	/** Maps the class-or-tag selection onto the class filter and tag clauses. InLogContext only names the owner in warnings. */
	void ApplyDeprecation(const UObject* InLogContext);
#endif

	/** Parses the clause lists. Call before anything below. */
	void Init();

	/** False when nothing can be gathered: an entry too long to be a tag, the class filter on without a class, or off
	 *  with neither Require clause set. OutWhyNot then receives the warning to show. */
	bool IsUsable(FText* OutWhyNot = nullptr) const;

	/** Class the world iteration is restricted to: ActorClass when filtering by class, AActor otherwise. */
	TSubclassOf<AActor> GetIterationClass() const;

	const TArray<FName>& GetRequireAllTags() const
	{
		return RequireAllTags;
	}

	const TArray<FName>& GetRequireAnyTags() const
	{
		return RequireAnyTags;
	}

	const TArray<FName>& GetExcludeTags() const
	{
		return ExcludeTags;
	}

	/** The narrowest PCG tracking key(s) every match satisfies, so edits to matching actors refresh the graph. Excluded
	 *  actors need no key of their own: they satisfy the same criterion, so they are already tracked. */
	void MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const;

	/** Node subtitle: the class when filtering by class, then each non-empty clause. Parses on its own; no Init needed. */
	FString GetTitleInformation() const;

private:
	TArray<FName> RequireAllTags;
	TArray<FName> RequireAnyTags;
	TArray<FName> ExcludeTags;
	bool bHasOverlongEntry = false;
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

	/** What an actor's tags decide. */
	enum class ETagVerdict : uint8
	{
		Drop,     // Fails a Require clause, or carries the PCG-spawned tag.
		Excluded, // Passes the Require clauses but carries an Exclude tag.
		Keep,
	};

	/**
	 * The selection's tag clauses compiled into one table, tested in a single pass over an actor's tags. Exact tags are
	 * FName compares; a wildcard pattern is string-matched once per distinct actor tag and the result remembered, so a
	 * matcher is stateful: one per sweep, game thread only.
	 */
	class PCGEXELEMENTSSPATIAL_API FTagMatcher
	{
	public:
		/** InSelection must have been Init()'d. */
		explicit FTagMatcher(const FPCGExActorSelectionDetails& InSelection);

		/** True when every actor is kept: no clause and no PCG-spawned exclusion. */
		bool IsEmpty() const
		{
			return bEmpty;
		}

		/** A Require All tag that is also excluded, by name or by an Exclude pattern, so every actor passing Require All is
		 *  excluded; None when there is none. */
		FName GetContradiction() const
		{
			return Contradiction;
		}

		/** When bResolveExcluded is false an excluded actor reads as Drop, which lets the scan stop at its first Exclude tag. */
		ETagVerdict Test(const TArray<FName>& InActorTags, bool bResolveExcluded);

	private:
		enum EFlags : uint8
		{
			FlagAny     = 1 << 0,
			FlagExclude = 1 << 1,
			FlagDrop    = 1 << 2,
		};

		/** What one tag contributes: clause flags, plus the Require All bits it satisfies as a slice of Bits. */
		struct FContribution
		{
			uint8 Flags = 0;
			int32 BitsStart = 0;
			int32 BitsNum = 0;
		};

		/** Index of the entry for InTag, created when missing; exact tags and patterns are separate tables. */
		int32 FindOrAddEntry(FName InTag, bool bPattern);
		const FContribution* FindExact(FName InTag) const;
		const FContribution* Classify(FName InTag);

		TArray<FName> ExactTags;
		TArray<FContribution> ExactContributions;
		TMap<FName, int32> ExactIndex; // Only filled past a few exact tags; a short linear scan beats hashing.

		TArray<FString> Patterns;
		TArray<FContribution> PatternContributions;

		/** Merged contribution of every actor tag seen so far; only used when there are patterns. */
		TMap<FName, FContribution> Memo;

		TArray<int32> Bits;
		TArray<uint64> RequiredWords;
		TArray<uint64> SeenWords;

		int32 NumRequiredBits = 0;
		int32 MinActorTags = 0;
		bool bHasAny = false;
		bool bEmpty = true;
		FName Contradiction = NAME_None;
	};

	/**
	 * One game-thread sweep: the resolved selection and cull, its tag matcher, and the snapshot lists actors are routed to.
	 * InSelection must have been Init()'d and must outlive the sweep.
	 * Tags are tested before the class whenever the caller has not applied the class filter itself, as it may be the costlier test.
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
		const AActor* Self = nullptr;

		FTagMatcher Tags;

		/** Snapshots a live actor the caller already restricted to the filter class, if its tags pass. */
		PCGEXELEMENTSSPATIAL_API void AddActor(const AActor* InActor);

		/** Same, with the class filter still to apply: InClassTest only runs once the tags pass. */
		PCGEXELEMENTSSPATIAL_API void AddActor(const AActor* InActor, TFunctionRef<bool()> InClassTest);

		/** Same for an actor only known by its tags, transform and world box, e.g. an unloaded World Partition actor. */
		PCGEXELEMENTSSPATIAL_API void AddBox(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds);

		/** Same, with the class filter still to apply: InClassTest only runs once the tags pass. */
		PCGEXELEMENTSSPATIAL_API void AddBox(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds, TFunctionRef<bool()> InClassTest);

	private:
		TArray<FSnapshot>* Route(const TArray<FName>& InActorTags);

		template <typename ClassTestFn>
		void AddActorImpl(const AActor* InActor, ClassTestFn&& InClassTest);

		template <typename ClassTestFn>
		void AddBoxImpl(const TArray<FName>& InActorTags, const FTransform& InActorTransform, const FBox& InWorldBounds, ClassTestFn&& InClassTest);
	};

	/**
	 * One allocation, then straight range writes; no metadata entries are created. Points are written in an order
	 * derived from their own values, so the output never depends on the order snapshots were gathered in.
	 */
	PCGEXELEMENTSSPATIAL_API void WritePoints(UPCGBasePointData* InData, const TArray<FSnapshot>& InSnapshots);
}
