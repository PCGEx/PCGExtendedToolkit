// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"

#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionTypeState.h"
#include "Helpers/PCGExArrayHelpers.h"

#include "PCGExOmniCollection.generated.h"

/**
 * One row of an Omni collection: a thin wrapper around a type-erased entry payload
 * (wrapped so future row metadata stays serialization-compatible). An unset payload is a
 * tolerated authoring state -- the row still consumes its raw index (skipped by iteration
 * and cache build), same contract as Variant rows.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Omni Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExOmniCollectionEntry
{
	GENERATED_BODY()

	/** Entry payload. Any concrete FPCGExAssetCollectionEntry type -- rows of different types coexist. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(BaseStruct="/Script/PCGExCollections.PCGExAssetCollectionEntry", ExcludeBaseStruct, ShowOnlyInnerProperties))
	FInstancedStruct Entry;

	FPCGExAssetCollectionEntry* GetPayload()
	{
		return Entry.GetMutablePtr<FPCGExAssetCollectionEntry>();
	}

	const FPCGExAssetCollectionEntry* GetPayload() const
	{
		return Entry.GetPtr<FPCGExAssetCollectionEntry>();
	}
};

/**
 * Type-erased asset collection: entries of ANY registered type coexist in one authored
 * list. Pick transport is type-blind and staged consumers dispatch per entry, so the whole
 * downstream pipeline works off the payloads directly. Collection-level settings are
 * provided through TypeGlobals blocks answering the type-globals seam -- an Omni host with
 * a matching block behaves exactly like the native typed collection.
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Omni", meta=(ToolTip = "A weighted collection of mixed entry types -- meshes, actors, levels, data assets and custom types in a single list."))
class PCGEXCOLLECTIONS_API UPCGExOmniCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()

public:
	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Omni;
	}

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExOmniCollectionEntry> Entries;

	/**
	 * Per-type collection-level globals blocks. One block per entry type at most (first
	 * struct match wins); entries whose type has no block fall back to entry-local settings.
	 * NOTE: raw FInstancedStruct copies are SHALLOW for Instanced subobjects -- cross-asset
	 * copies must DuplicateObject them into the destination asset.
	 */
	UPROPERTY(EditAnywhere, Category = "Settings|Global", meta=(BaseStruct="/Script/PCGExCollections.PCGExCollectionTypeGlobals", ExcludeBaseStruct))
	TArray<FInstancedStruct> TypeGlobals;

	/**
	 * Per-type machinery state/processor blocks (see UPCGExCollectionTypeState). One instance
	 * per present entry type whose registry entry declares a StateClass -- ensured
	 * automatically at the start of every editor staging rebuild session. Owns the mutable
	 * cross-entry state (e.g. the PCGDataAsset shared collections) and receives the host
	 * lifecycle dispatches. Serialized (cooked too -- state may carry runtime subobjects).
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Settings|Global", meta=(DisplayName="Type Machinery"))
	TArray<TObjectPtr<UPCGExCollectionTypeState>> TypeStates;

	/** First state instance of (or derived from) StateClass; null when absent (see base --
	 *  accessor semantics keep the typed FindTypeState<T>() template cast-safe). */
	using UPCGExAssetCollection::FindTypeState; // keep the base template visible
	virtual UPCGExCollectionTypeState* FindTypeState(const UClass* StateClass) const override;

	/** First state instance sharing StateClass's type SLOT (base and derived state classes
	 *  answer the same machinery -- PCGExCollectionHelpers::MatchesTypeSlot, both directions).
	 *  Occupancy check for ensure/cleanup; NOT cast-safe, use FindTypeState for access. */
	UPCGExCollectionTypeState* FindTypeStateForSlot(const UClass* StateClass) const;

	/** True for any type with a registered StateClass (this host can run its machinery). */
	virtual bool SupportsTypeMachinery(PCGExAssetCollection::FTypeId TypeId) const override;

	//~ Lifecycle dispatch into type states
	virtual void Serialize(FArchive& Ar) override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;

	//~ UPCGExAssetCollection storage interface -- rows wrap payloads, so the
	// PCGEX_ASSET_COLLECTION_BODY macro doesn't apply; virtuals resolve through the wrapper.
	virtual bool IsValidIndex(int32 InIndex) const override;
	virtual int32 NumEntries() const override;
	virtual void InitNumEntries(int32 InNum) override;
	virtual void BuildCache() override;
	virtual void ForEachEntry(FForEachConstEntryFunc Iterator) const override;
	virtual void ForEachEntry(FForEachEntryFunc Iterator) override;
	virtual void Sort(FSortEntriesFunc Predicate) override;

	/**
	 * Append a row default-initialized as EntryStruct (any entry-derived struct, base
	 * included -- what subcollection rows use); null if invalid. Caller owns Modify /
	 * MarkPackageDirty / staging rebuild.
	 */
	FPCGExAssetCollectionEntry* AddEntryOfType(const UScriptStruct* EntryStruct);

#if WITH_EDITOR
	/**
	 * Append every entry of the given source collections into this Omni (conversion with
	 * one source, merge with several). Sources are untouched; returns the appended count.
	 *
	 * - Entries copy as payload rows of their exact type, as NEW identities (EntryId
	 *   re-minted); source CollectionTags bake into entry Tags; Instanced subobjects inside
	 *   payloads are DuplicateObject'd into this asset. Subcollection rows keep their
	 *   shared collection reference.
	 * - Each globals block a source provides (GetTypeGlobalsStructs) becomes a TypeGlobals
	 *   block, subobjects duplicated. On a value conflict, behavior wins over the block:
	 *   a block THIS call installed is removed and both contributors bake via
	 *   ResolveGlobalsToLocal; a pre-existing block stays (incoming bakes + warning).
	 * - The collection-level property schema is re-derived afterwards.
	 */
	int32 EDITOR_AppendCollections(TConstArrayView<UPCGExAssetCollection*> InSources);

	virtual void EDITOR_AddSubCollectionEntries(const TArray<UPCGExAssetCollection*>& InSubCollections) override;

	virtual const UScriptStruct* EDITOR_GetEntryScriptStruct(int32 RawIndex) const override;
	virtual void EDITOR_GetAddableEntryTypes(TArray<const UScriptStruct*>& OutTypes) const override;
	virtual FPCGExAssetCollectionEntry* EDITOR_AddEntry(const UScriptStruct* EntryStruct = nullptr) override;

	/** Ensures type states for present entry types, runs the actor-component property scan
	 *  over actor-typed entries (matching a native actor collection), then dispatches the
	 *  post-rebuild hook into every type state. */
	virtual void EDITOR_OnPostStagingRebuild() override;

	/**
	 * Ensure the per-type setup for every present LEAF entry type: a TypeGlobals block
	 * (seeded from the registered typed collection's CDO through the globals seam, so
	 * defaults -- including settings-driven instanced subobjects like the actor bounds
	 * evaluator -- match a freshly created typed collection) and a TypeStates instance
	 * (class defaults mirror the typed collection's members). Type resolution is
	 * lineage-aware (FTypeRegistry::GetInfoResolved) and existence checks are slot-based,
	 * matching the staging capability guard. Runs eagerly on every entry-add path (per
	 * added type) and as a safety net at the start of each post-rebuild session.
	 * Idempotent; calls Modify() only when adding. Returns true when anything was added.
	 */
	bool EDITOR_EnsureTypeSetup();

	/** Single-type slice of EDITOR_EnsureTypeSetup -- what entry-add paths call so a K-row
	 *  ingestion doesn't rescan the whole collection per row. SeedSource, when given (merge /
	 *  conversion), is offered to NEWLY created states via OnAddedToHost so they can adopt
	 *  its settings; existing states are never touched (they get OnSeedSourceIgnored). */
	bool EDITOR_EnsureTypeSetupForType(PCGExAssetCollection::FTypeId TypeId, const UPCGExAssetCollection* SeedSource = nullptr);

	/**
	 * State-only half of the per-type ensure. EDITOR_AppendCollections uses this MID-CALL:
	 * states must exist early to receive the per-source seed, but installing a missing
	 * GLOBALS block between sources would corrupt the intra-call block conflict resolution
	 * (a later source's authored block would mistake an auto-installed CDO block for a
	 * user-authored one and bake itself away) -- blocks are left to the tail rebuild's full
	 * ensure, which runs after every source has had its say.
	 */
	bool EDITOR_EnsureTypeStateForType(PCGExAssetCollection::FTypeId TypeId, const UPCGExAssetCollection* SeedSource);

	/**
	 * Counterpart affordance (user-triggered, never automatic -- blocks/states may carry
	 * user edits): remove TypeGlobals blocks and TypeStates whose type has no leaf entry
	 * left in the collection. Conservative: only setup owned by a REGISTERED type is ever
	 * removed (unknown/third-party leftovers are kept); empty blocks and null state slots
	 * count as debris. Fires PostEditChange when anything was removed (staging depends on
	 * globals). Returns the number of removed items.
	 */
	int32 EDITOR_CleanupUnusedTypeSetup();

	virtual void GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;

protected:
	/**
	 * Routes each dropped asset to the highest-priority type whose DetectSourceAsset claims
	 * it. Assets already referenced by a row of the same entry type are skipped.
	 */
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;

	/** Distinct type ids of leaf (non-subcollection) entries. Shared presence definition for
	 *  ensure/cleanup -- the pair must always agree on what "present" means. */
	TSet<PCGExAssetCollection::FTypeId> EDITOR_CollectPresentLeafTypeIds() const;

public:
#endif

	virtual void GetTypeGlobalsStructs(TArray<const UScriptStruct*>& OutStructs) const override;

protected:
	virtual bool GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const override;

	virtual const FPCGExAssetCollectionEntry* GetEntryAtRawIndex(int32 Index) const override;
	virtual FPCGExAssetCollectionEntry* GetMutableEntryAtRawIndex(int32 Index) override;
};
