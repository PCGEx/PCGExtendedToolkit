// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

#include "PCGExActorSelectionDetails.generated.h"

class AActor;
class IPCGGraphExecutionSource;
class UWorld;
struct FPCGActorSelectorSettings;
struct FPCGComponentSelectorSettings;
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

/**
 * Where a selection draws its candidates from. Values are the stock EPCGActorFilter values on purpose: an override pin
 * writes the raw integer, so a legacy ActorFilter pin keeps its meaning. Never renumber.
 */
UENUM()
enum class EPCGExActorScope : uint8
{
	World    = 3 UMETA(DisplayName = "World", Tooltip = "Every loaded actor of the world, filtered by class and tags."),
	Self     = 0 UMETA(DisplayName = "Self", Tooltip = "The execution target: the component owner, or the partition actor during a partitioned run."),
	Parent   = 1 UMETA(DisplayName = "Parent", Tooltip = "The parent actor of Self (child actor components); Self when it has none."),
	Root     = 2 UMETA(DisplayName = "Root", Tooltip = "The top of Self's parent actor chain."),
	Original = 4 UMETA(DisplayName = "Original", Tooltip = "The owner of the original component during a partitioned run; Self otherwise."),
};

namespace PCGExActorSelection
{
	/** Parsed clauses a tag matcher is built from. Borrows: the owner must outlive the matcher's construction. */
	struct FTagClauseView
	{
		const TArray<FName>& RequireAll;
		const TArray<FName>& RequireAny;
		const TArray<FName>& Exclude;
		bool bAllowWildcards = false;

		/** A tag that drops whatever carries it, before any clause: None for no such tag. */
		FName DropTag = NAME_None;
	};
}

/**
 * Three comma-separated tag clauses matched as exact names (case-insensitive) unless wildcards are opted in. A blank
 * clause constrains nothing, so an empty filter keeps everything.
 */
USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/tag-filter-details"))
struct PCGEXFOUNDATIONS_API FPCGExTagFilterDetails
{
	GENERATED_BODY()

	/** Comma-separated tags that must all be carried. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString RequireAll;

	/** Comma-separated tags of which at least one must be carried. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString RequireAny;

	/** Comma-separated tags that exclude whatever carries one of them. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	FString Exclude;

	/** Treat '*' and '?' in any clause as wildcards. A pattern is string-matched once per distinct tag, then remembered. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bAllowWildcards = false;

#if WITH_EDITOR
	/** Maps a stock component selector: its tag becomes Require Any. A class selection is the host's to handle. */
	void ApplyDeprecation(const FPCGComponentSelectorSettings& InLegacy, const UObject* InLogContext);
#endif

	/** Parses the clause lists; IsUsable and GetClauseView read the result. */
	void Init();

	/** False when an entry is too long to be a tag; OutWhyNot then receives the warning to show. */
	bool IsUsable(FText* OutWhyNot = nullptr) const;

	/** True when no clause is set: every tag list passes. */
	bool IsEmpty() const
	{
		return RequireAllTags.IsEmpty() && RequireAnyTags.IsEmpty() && ExcludeTags.IsEmpty();
	}

	const TArray<FName>& GetRequireAllTags() const { return RequireAllTags; }
	const TArray<FName>& GetRequireAnyTags() const { return RequireAnyTags; }
	const TArray<FName>& GetExcludeTags() const { return ExcludeTags; }

	/** InDropTag drops whatever carries it regardless of the clauses, e.g. the PCG generated component tag. */
	PCGExActorSelection::FTagClauseView GetClauseView(FName InDropTag = NAME_None) const;

	/** Each non-empty clause, for a node subtitle. Parses on its own; no Init needed. */
	FString GetTitleInformation() const;

private:
	TArray<FName> RequireAllTags;
	TArray<FName> RequireAnyTags;
	TArray<FName> ExcludeTags;
	bool bHasOverlongEntry = false;
};

/**
 * Which actors a sweep gathers: a scope, then (in World scope, or when children are included) an optional class filter
 * and three comma-separated tag clauses matched as exact names (case-insensitive) unless wildcards are opted in.
 * Selects nothing in World scope without the class filter or a Require clause.
 */
USTRUCT(BlueprintType, meta=(PCGExNodeLibraryDoc="common-settings/actor-selection-details"))
struct PCGEXFOUNDATIONS_API FPCGExActorSelectionDetails
{
	GENERATED_BODY()

	FPCGExActorSelectionDetails();

	/** Where candidates come from. Self-relative scopes take their actor as-is unless children are included. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	EPCGExActorScope Scope = EPCGExActorScope::World;

	/** Also consider the actors attached to the scope actor, recursively; the class filter and tag clauses then apply. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Scope != EPCGExActorScope::World", EditConditionHides))
	bool bIncludeChildren = false;

	/** Restrict the sweep to one actor class. Like the tag clauses, only tested in World scope or when children are included. */
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

	/** Only keep actors whose bounds overlap the executing component's bounds (its partition cell when partitioned). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable))
	bool bMustOverlapSelf = false;

	/** Skip the actor that owns the executing PCG component, and the actors it spawned through child actor components. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta = (PCG_Overridable, EditCondition = "Scope == EPCGExActorScope::World", EditConditionHides))
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

	/** Maps a stock actor selector (scope, class or tag, children, overlap, self) onto this struct. Stock never skipped
	 *  PCG-spawned actors, so that flag is turned off; features without a counterpart are warned about and dropped. */
	void ApplyDeprecation(const FPCGActorSelectorSettings& InLegacy, const UObject* InLogContext);
#endif

	/** Parses the clause lists; IsUsable, MakeTrackingKeys and the tag matcher read the result. */
	void Init();

	/** True when the class filter and tag clauses are tested: World scope, or a self-relative scope with children. */
	bool AppliesCriteria() const
	{
		return Scope == EPCGExActorScope::World || bIncludeChildren;
	}

	/** False when nothing can be gathered: an entry too long to be a tag, the class filter on without a class, or off
	 *  with neither Require clause set. Always true when the criteria do not apply. OutWhyNot then receives the warning. */
	bool IsUsable(FText* OutWhyNot = nullptr) const;

	/** Class the world iteration is restricted to: ActorClass when filtering by class, AActor otherwise. */
	TSubclassOf<AActor> GetIterationClass() const;

	const TArray<FName>& GetRequireAllTags() const { return RequireAllTags; }
	const TArray<FName>& GetRequireAnyTags() const { return RequireAnyTags; }
	const TArray<FName>& GetExcludeTags() const { return ExcludeTags; }

	/** The clauses, with the PCG-spawned actor tag as drop tag when those actors are ignored. */
	PCGExActorSelection::FTagClauseView GetClauseView() const;

	/** The narrowest PCG tracking key(s) every match satisfies, so edits to matching actors refresh the graph. Excluded
	 *  actors need no key of their own: they satisfy the same criterion, so they are already tracked. */
	void MakeTrackingKeys(TArray<FPCGSelectionKey>& OutKeys) const;

	/** Node subtitle: the scope when not World, the class when filtering by class, then each non-empty clause. Parses on its own; no Init needed. */
	FString GetTitleInformation() const;

private:
	TArray<FName> RequireAllTags;
	TArray<FName> RequireAnyTags;
	TArray<FName> ExcludeTags;
	bool bHasOverlongEntry = false;
};

namespace PCGExActorSelection
{
	/** What an actor's tags decide. */
	enum class ETagVerdict : uint8
	{
		Drop,     // Fails a Require clause, or carries the drop tag.
		Excluded, // Passes the Require clauses but carries an Exclude tag.
		Keep,
	};

	/**
	 * Tag clauses compiled into one table, tested in a single pass over a tag list. Exact tags are FName compares; a
	 * wildcard pattern is string-matched once per distinct tag and the result remembered, so a matcher is stateful:
	 * one per sweep, game thread only.
	 */
	class PCGEXFOUNDATIONS_API FTagMatcher
	{
	public:
		explicit FTagMatcher(const FTagClauseView& InClauses);

		/** InSelection must have been Init()'d. */
		explicit FTagMatcher(const FPCGExActorSelectionDetails& InSelection);

		/** InFilter must have been Init()'d. */
		explicit FTagMatcher(const FPCGExTagFilterDetails& InFilter, FName InDropTag = NAME_None);

		/** True when every tag list is kept: no clause and no drop tag. */
		bool IsEmpty() const
		{
			return bEmpty;
		}

		/** A required tag that is also excluded (by Exclude, by an Exclude pattern, or as the drop tag), so nothing can
		 *  be kept: any Require All entry, or the first Require Any entry when all of them are. None otherwise. */
		FName GetContradiction() const
		{
			return Contradiction;
		}

		/** When bResolveExcluded is false an excluded list reads as Drop, which lets the scan stop at its first Exclude tag. */
		ETagVerdict Test(const TArray<FName>& InTags, bool bResolveExcluded);

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
		FName FindContradiction(const TArray<FName>& InRequireAnyTags) const;

		TArray<FName> ExactTags;
		TArray<FContribution> ExactContributions;
		TMap<FName, int32> ExactIndex; // Only filled past a few exact tags; a short linear scan beats hashing.

		TArray<FString> Patterns;
		TArray<FContribution> PatternContributions;

		/** Merged contribution of every tag seen so far; only used when there are patterns. */
		TMap<FName, FContribution> Memo;

		TArray<int32> Bits;
		TArray<uint64> RequiredWords;
		TArray<uint64> SeenWords;

		int32 NumRequiredBits = 0;
		int32 MinTags = 0;
		bool bHasAny = false;
		bool bEmpty = true;
		FName Contradiction = NAME_None;
	};

	/** The execution target: the component owner, or the partition actor during a partitioned run. */
	PCGEXFOUNDATIONS_API AActor* ResolveSelf(const FPCGExContext* InContext);

	/** The owner of the original component during a partitioned run; null when there is none. */
	PCGEXFOUNDATIONS_API AActor* ResolveOriginal(const FPCGExContext* InContext);

	/** True for Self itself and for every actor whose child-actor-component parent chain reaches Self. Null Self: false. */
	PCGEXFOUNDATIONS_API bool IsSelfOrChildOfSelf(const AActor* InActor, const AActor* InSelf);

	/**
	 * One game-thread run of a selection: scope actors or a world iteration, the self skip, the overlap test and the
	 * tag matcher. The selection must have been Init()'d and must outlive the query.
	 */
	struct PCGEXFOUNDATIONS_API FQuery
	{
		/**
		 * Resolves everything the run needs. False, after a graph warning, when nothing can be selected: unusable
		 * criteria, no world, or Must Overlap Self with no valid self bounds. With bApplyOverlap false the caller owns
		 * the overlap test (a tighter cull); the self bounds are still resolved and checked.
		 */
		static bool Make(const FPCGExActorSelectionDetails& InSelection, FPCGExContext* InContext, FQuery& OutQuery, bool bApplyOverlap = true);

		/** Every selected actor, in iteration order. */
		void Run(TFunctionRef<void(AActor*)> InKeep);

		/**
		 * Every candidate before the tag test and the overlap test: the world iteration (class-restricted) with the self
		 * skip, or the scope actors (class-tested when the criteria apply). For callers that route on the tag verdict.
		 */
		void ForEachCandidate(TFunctionRef<void(AActor*)> InVisit);

		/** True when InActor passes the overlap test, or when there is none. */
		bool Overlaps(const AActor* InActor) const;

		const FPCGExActorSelectionDetails* Selection = nullptr;
		const FPCGExContext* Context = nullptr;
		UWorld* World = nullptr;

		/** Set when Scope is World and the selection ignores self. */
		const AActor* Self = nullptr;

		/** Valid when Must Overlap Self is on. */
		FBox SelfBounds = FBox(ForceInit);
		bool bApplyOverlap = true;

		/** Built from the selection; empty when the criteria do not apply. */
		TOptional<FTagMatcher> Tags;

	private:
		void GatherScopeActors(TArray<AActor*>& OutActors) const;
	};

	/** Self bounds enter the cache key only when they drive the selection. */
	PCGEXFOUNDATIONS_API void CombineSelfBoundsCrc(const FPCGGetDependenciesCrcParams& InParams, bool bMustOverlapSelf, FPCGCrc& InOutCrc);

#if WITH_EDITOR
	/** Dynamic tracking keys when the selection is overridden at run time; bIsCulled marks keys whose actors only matter near self. */
	PCGEXFOUNDATIONS_API void RegisterDynamicTracking(FPCGExContext* InContext, const FPCGExActorSelectionDetails& InSelection, bool bIsCulled, FName InSelectionProperty);
#endif
}
