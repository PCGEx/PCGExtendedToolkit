// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

class FObjectPropertyBase;
#include "PCGExAssetCollection.h"
#include "PCGParamData.h"
#include "Core/PCGExContext.h"
#include "Details/PCGExRoamingAssetCollectionDetails.h"
#include "Details/PCGExStagingDetails.h"

struct FAssetData;
struct FStreamableHandle;
class UPCGExVariantCollection;
struct FPCGExBaseTagFilterDetails;

/**
 * Collection Helper Functions
 *
 * Simplified API for working with asset collections: convenience functions and attribute set building.
 */

namespace PCGExCollectionHelpers
{
	/**
	 * Registry-driven entry-payload resolution: the highest-priority type whose DetectSourceAsset claims
	 * an asset builds its payload (MakeEntryFromSourceAsset, else InitializeAs + SetAssetPath). Detectors
	 * are snapshotted at construction (copied out of the registry -- interior pointers never outlive its
	 * lock), so build one per operation, not per asset. Shared by Omni drop routing and attribute-set
	 * builds.
	 */
	class PCGEXCOLLECTIONS_API FSourceAssetResolver
	{
	public:
		FSourceAssetResolver();
		~FSourceAssetResolver();
		FSourceAssetResolver(const FSourceAssetResolver&) = delete;
		FSourceAssetResolver& operator=(const FSourceAssetResolver&) = delete;

		/** Payload for a registry row; false when no type claims it (Generic catches every registered asset). */
		bool Resolve(const FAssetData& InAsset, FInstancedStruct& OutPayload) const;

		/**
		 * Payload for an object path. Registry row first (no load); paths without one (class paths,
		 * unscanned assets) load the object and resolve through it. Loads stay pinned for the resolver's
		 * lifetime.
		 */
		bool ResolvePath(const FSoftObjectPath& InPath, FInstancedStruct& OutPayload, FPCGExContext* InContext = nullptr);

	private:
		TArray<PCGExAssetCollection::FTypeInfo> Detectors;
		TArray<TSharedPtr<FStreamableHandle>> Handles;
	};

	/**
	 * Per-type slot identity: base and derived globals blocks / machinery state classes
	 * answer the same queries, so they share ONE slot. Both directions on purpose -- see
	 * UPCGExOmniCollection::EDITOR_AppendCollections' conflict handling, which set the
	 * precedent. Every block/state ownership or existence check must use this (one-directional
	 * IsChildOf makes the answer depend on registry iteration order).
	 */
	inline bool MatchesTypeSlot(const UScriptStruct* A, const UScriptStruct* B)
	{
		return A && B && (A->IsChildOf(B) || B->IsChildOf(A));
	}

	inline bool MatchesTypeSlot(const UClass* A, const UClass* B)
	{
		return A && B && (A->IsChildOf(B) || B->IsChildOf(A));
	}

	/**
	 * Append one entry per attribute-set row to InCollection: entry type from the registry (FSourceAssetResolver,
	 * unclaimed assets become Generic), row appended through AddEntryOfType (a typed host rejects foreign
	 * types -- skipped with a warning), extra attributes mapped to custom properties (schema on the
	 * collection, enabled override per entry). Outside the editor, non-runtime-stageable types (Actor,
	 * Level) are authored with the details' default staging bounds, which are also copied onto
	 * the host. Returns true when at least one entry was appended.
	 */
	PCGEXCOLLECTIONS_API
	bool BuildFromAttributeSet(
		UPCGExAssetCollection* InCollection,
		FPCGExContext* InContext,
		const UPCGParamData* InAttributeSet,
		const FPCGExRoamingAssetCollectionDetails& Details,
		bool bBuildStaging = false);

	/** BuildFromAttributeSet over the first attribute set found on InputPin. */
	PCGEXCOLLECTIONS_API
	bool BuildFromAttributeSet(
		UPCGExAssetCollection* InCollection,
		FPCGExContext* InContext,
		FName InputPin,
		const FPCGExRoamingAssetCollectionDetails& Details,
		bool bBuildStaging);

	/**
	 * Whether a path names an asset the registry knows from disk is not a material, without loading it. Unknown paths,
	 * redirectors (renamed materials) and classes the registry can't place count as materials. Caches per class: keep one
	 * per batch of checks.
	 */
	class PCGEXCOLLECTIONS_API FNonMaterialCheck
	{
	public:
		bool IsKnownNonMaterial(const FSoftObjectPath& InPath);

	private:
		TMap<FTopLevelAssetPath, bool> NonMaterialByClass;
	};

	/**
	 * Appends one MaterialSwaps row per reachable attribute-set row (paths: Soft Object Path or String; with InTagFilter,
	 * each clause is its constant or a per-row Name / String). OutSkipped counts empty paths, unreadable tags, unreachable
	 * rows and replacements the registry knows are not materials. Returns the number appended, -1 on a bad column (logged).
	 */
	PCGEXCOLLECTIONS_API
	int32 AppendMaterialSwapsFromAttributeSet(
		UPCGExVariantCollection* InVariant,
		FPCGExContext* InContext,
		const UPCGParamData* InAttributeSet,
		FName InMatchAttribute,
		FName InReplacementAttribute,
		const FPCGExBaseTagFilterDetails* InTagFilter,
		int32& OutSkipped);

	/**
	 * Accumulate tags from entry and potentially its subcollection hierarchy
	 * @param Entry The entry to get tags from
	 * @param TagInheritance Bitmask of EPCGExAssetTagInheritance flags
	 * @param OutTags Set to append tags to
	 * @param InHost The collection holding Entry; without it the Asset source reads no category tags
	 */
	PCGEXCOLLECTIONS_API
	void AccumulateTags(
		const FPCGExAssetCollectionEntry* Entry,
		uint8 TagInheritance,
		TSet<FName>& OutTags,
		const UPCGExAssetCollection* InHost = nullptr);

	/**
	 * Classify a collection's LEAF entries for actor-vs-asset output declaration (actor =
	 * asset CLASS, everything else = asset PATH); heterogeneous hosts may hold both.
	 * Subcollection entries are skipped. A typed Actor collection reports bOutAnyActor even
	 * when empty (legacy declaration behavior).
	 */
	PCGEXCOLLECTIONS_API
	void GetEntryAssetHalves(const UPCGExAssetCollection* Collection, bool& bOutAnyActor, bool& bOutAnyNonActor);

#if WITH_EDITOR
	/** Visits every top-level Instanced object reference of Struct in StructMemory, with its current value. */
	PCGEXCOLLECTIONS_API
	void ForEachInstancedObjectRef(const UScriptStruct* Struct, void* StructMemory, TFunctionRef<void(const FObjectPropertyBase*, UObject*)> Visit);

	/**
	 * Deep-copy Instanced subobjects referenced by a struct into a new owner. Raw struct
	 * copies are SHALLOW for object refs and sharing EditInlineNew subobjects across assets
	 * is illegal -- run this on every cross-asset copy of a globals block. An entry copied from
	 * another entry goes through DetachCopiedEntry instead. Top-level properties only (built-in
	 * structs keep instanced refs flat).
	 */
	PCGEXCOLLECTIONS_API
	void DuplicateInstancedSubobjects(const UScriptStruct* Struct, void* StructMemory, UObject* NewOuter);

	/**
	 * Make an entry payload just copied from OriginalMemory its own -- run on every entry copy, within one collection
	 * or across assets: EDITOR_OnCopied first, then every top-level Instanced subobject still shared with the original
	 * is duplicated into NewHost, even one already outered there. OriginalMemory is an instance of Struct (possibly a
	 * base of the payload's own) or null, in which case nothing is duplicated.
	 */
	PCGEXCOLLECTIONS_API
	void DetachCopiedEntry(const UScriptStruct* Struct, void* StructMemory, const void* OriginalMemory, UPCGExAssetCollection* NewHost);

	/**
	 * Move a struct's Instanced subobjects to the transient package and null the refs. For entries about
	 * to be overwritten: an unreferenced inner left under its outer still shows up in save-time traversal.
	 * A subobject SharedWith (another instance of Struct) holds at the same property is only nulled --
	 * that instance still owns it. Top-level properties only, like DuplicateInstancedSubobjects.
	 */
	PCGEXCOLLECTIONS_API
	void RetireInstancedSubobjects(const UScriptStruct* Struct, void* StructMemory, const void* SharedWith = nullptr);

	/**
	 * Rename a struct's Instanced subobjects under NewOuter (no copy). For subobjects the struct is the
	 * SOLE owner of -- freshly minted by an export handler -- where a duplicate would only leave an
	 * orphan behind. Top-level properties only.
	 */
	PCGEXCOLLECTIONS_API
	void ReparentInstancedSubobjects(const UScriptStruct* Struct, void* StructMemory, UObject* NewOuter);
#endif
}
