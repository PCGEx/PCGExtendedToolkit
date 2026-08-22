// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGExPropertySchema.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGExSchemaMerging.generated.h"

/**
 * Policy for resolving same-name collisions when merging multiple property schema sources.
 *
 * Sources are processed in order; "earlier" / "later" below refers to that order.
 * The first source is typically the canonical / user-authored schema, so most callers
 * want FirstWins or StrictTypeMatch (defaults that protect manual authoring).
 */
UENUM(BlueprintType)
enum class EPCGExSchemaMergePolicy : uint8
{
	/** Earlier source wins; subsequent same-name entries are dropped and reported as conflicts. */
	FirstWins = 0,

	/** Later source wins; earlier same-name entry is replaced and the displacement reported as a conflict. */
	LastWins = 1,

	/**
	 * Same-name + same UScriptStruct: silent dedupe (earlier kept).
	 * Same-name + different UScriptStruct: drop the later entry and report a conflict.
	 * This is the recommended default when merging manually-authored schemas with auto-scanned ones.
	 */
	StrictTypeMatch = 2,

	/**
	 * Any duplicate name is treated as a hard error. Conflicts are collected and the merge
	 * yields an empty Merged array; callers should surface the conflict list and abort.
	 */
	Reject = 3,
};

namespace PCGExProperties
{
	/**
	 * Describes one same-name collision encountered during a merge.
	 *
	 * IncomingType is the type the later source tried to introduce; ExistingType is the type
	 * already in the merged set when the conflict was detected. Under LastWins, ExistingType
	 * is what got replaced; under FirstWins / StrictTypeMatch / Reject, IncomingType is what
	 * got dropped (StrictTypeMatch only records the case where types differ).
	 *
	 * UScriptStruct pointers are raw because UScriptStructs are permanent native objects
	 * (registered on the engine's struct table, never GC'd).
	 */
	struct PCGEXPROPERTIES_API FSchemaMergeConflict
	{
		FName PropertyName;
		int32 IncomingSourceIdx = INDEX_NONE;
		int32 IncomingLocalIdx = INDEX_NONE;
		int32 ExistingSourceIdx = INDEX_NONE;
		int32 ExistingMergedIdx = INDEX_NONE;
		const UScriptStruct* IncomingType = nullptr;
		const UScriptStruct* ExistingType = nullptr;
	};

	/**
	 * Output of PCGExProperties::MergeSchemas.
	 *
	 * Merged is the unified flat schema (parallel-array form used by FPCGExPropertyOverrides::SyncToSchema).
	 *
	 * SourceToMergedIdx[sourceIdx][localIdx] -> index into Merged, or INDEX_NONE if that input
	 * property was dropped by the policy. Use this when you need to write per-source values into
	 * the correct merged slot (e.g. populating per-entry PropertyOverrides from a per-actor scan).
	 *
	 * Conflicts records every collision the policy resolved, for diagnostics.
	 */
	struct PCGEXPROPERTIES_API FSchemaMergeResult
	{
		TArray<FInstancedStruct> Merged;
		TArray<TArray<int32>> SourceToMergedIdx;
		TArray<FSchemaMergeConflict> Conflicts;

		bool HasConflicts() const
		{
			return !Conflicts.IsEmpty();
		}

		void Reset()
		{
			Merged.Reset();
			SourceToMergedIdx.Reset();
			Conflicts.Reset();
		}
	};

	/**
	 * Merge multiple property schema sources into a single flat schema.
	 *
	 * Each source is a compiled schema array (the output of FPCGExPropertySchemaCollection::BuildSchema).
	 * Caller is expected to have called SyncAllSchemas on the underlying collections so each
	 * FInstancedStruct's FPCGExProperty::PropertyName field is up-to-date.
	 *
	 * Source order is significant: see EPCGExSchemaMergePolicy.
	 *
	 * Properties with PropertyName == NAME_None are skipped (cannot be matched). Their slot in
	 * SourceToMergedIdx is INDEX_NONE.
	 */
	PCGEXPROPERTIES_API FSchemaMergeResult MergeSchemas(
		TConstArrayView<TArray<FInstancedStruct>> Sources,
		EPCGExSchemaMergePolicy Policy = EPCGExSchemaMergePolicy::StrictTypeMatch);

	/**
	 * Rebuild a schema collection's Schemas array from the merged FInstancedStruct list
	 * produced by MergeSchemas, preserving the FPCGExPropertySchema::HeaderId of every
	 * existing entry whose Name survived the merge. New properties get fresh HeaderIds
	 * via the FPCGExPropertySchema default constructor.
	 *
	 * Encapsulates the OldByName-cache → Reset → AddDefaulted-per-merged-prop → SyncPropertyName
	 * pattern used by both the actor-component scan and the entry-overrides-union scan paths.
	 */
	PCGEXPROPERTIES_API void ApplyMergeResultToSchemas(
		FPCGExPropertySchemaCollection& InOutCollection,
		const TArray<FInstancedStruct>& MergedProperties);

	/**
	 * Emit a UE_LOG(Warning) for every conflict the merge resolved. ContextOwner names the
	 * collection (or other owning object) in the log line so users can trace which asset
	 * surfaced the collision.
	 */
	PCGEXPROPERTIES_API void LogSchemaConflicts(
		const FSchemaMergeResult& MergeResult,
		const UObject* ContextOwner);

	/**
	 * Per-property-name unanimity vote across multiple FInstancedStruct sources.
	 *
	 * For each ValueSet (typically one per contributing BP class / per-export view), every
	 * FInstancedStruct is keyed by its FPCGExProperty::PropertyName. Per property name:
	 *   - If every encountered value compares equal, emit the value.
	 *   - Otherwise, emit the first match found in FallbackSets (e.g. asset-default views).
	 *   - If neither path applies, the property is dropped from the aggregate.
	 *
	 * Used to derive a "common-ancestor" defaults view across heterogeneous contributors so
	 * a downstream MergeSchemas pass can seed CollectionProperties without falling back to
	 * whichever contributor happened to be iterated first.
	 *
	 * Empty ValueSets -> empty result. Emission order is the TMap walk order (non-deterministic).
	 */
	PCGEXPROPERTIES_API TArray<FInstancedStruct> AggregateAgreedValuesByName(
		TConstArrayView<TConstArrayView<FInstancedStruct>> ValueSets,
		TConstArrayView<TConstArrayView<FInstancedStruct>> FallbackSets = {});
}
