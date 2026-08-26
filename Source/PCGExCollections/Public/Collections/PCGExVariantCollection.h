// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"

#include "PCGExVariantCollection.generated.h"

/**
 * One override row inside a variant source group: replaces the source entry identified by
 * SourceEntryId with the entry payload held in Entry. Cross-type replacement is legal --
 * a mesh entry may be replaced by an actor entry and vice versa; consumers type-check
 * resolved entries, not their host collection.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExVariantEntryOverride
{
	GENERATED_BODY()

	/**
	 * EntryId of the source entry this row replaces (see FPCGExAssetCollectionEntry::EntryId).
	 * 0 = unbound row (never swapped). Ids are minted on the source by SyncEntryIds -- staging
	 * rebuilds do it, and the editor heals missing ids on load, in the variant grid, and from
	 * the entry picker (minting dirties the source, which then needs a save to persist).
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	int32 SourceEntryId = 0;

	/** Replacement entry. Any concrete FPCGExAssetCollectionEntry type. Unset = row inert. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(BaseStruct="/Script/PCGExCollections.PCGExAssetCollectionEntry", ExcludeBaseStruct))
	FInstancedStruct Entry;
};

/** A group of overrides against one source collection. Mappings derive from the live source at consumption time. */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExVariantSource
{
	GENERATED_BODY()

	/** The source collection whose picks this group can redirect. Hard ref (SubCollection rationale): loads with the variant, so mappings resolve live. */
	UPROPERTY(EditAnywhere, Category = Settings, DisplayName="Source")
	TObjectPtr<UPCGExAssetCollection> SourceCollection;

	/** Pre-hard-ref serialized carrier (still reads old "Source" data). Migrated on PostLoad. */
	UPROPERTY(meta=(DeprecatedProperty, ScriptNoExport))
	TSoftObjectPtr<UPCGExAssetCollection> Source_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPCGExVariantEntryOverride> Overrides;
};

/**
 * Authoring shorthand: "any source entry staging this asset swaps to this payload."
 * Resolved live by BuildGroupMapping against the declared sources -- a rule with no match in
 * any source simply maps nothing. Explicit per-entry rows always take precedence over rules.
 */
USTRUCT(BlueprintType)
struct PCGEXCOLLECTIONS_API FPCGExVariantPathOverride
{
	GENERATED_BODY()

	/** Source entries whose staged asset path (Staging.Path) equals this swap to Entry. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FSoftObjectPath MatchAsset;

	/** Replacement entry. Any concrete FPCGExAssetCollectionEntry type. Unset = rule inert. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(BaseStruct="/Script/PCGExCollections.PCGExAssetCollectionEntry", ExcludeBaseStruct))
	FInstancedStruct Entry;
};

/**
 * A collection that themes other collections: per source collection, a set of override rows
 * keyed by stable EntryId, each carrying a full replacement entry of any concrete type
 * (heterogeneous by design -- mesh and actor entries can coexist).
 *
 * It is a first-class UPCGExAssetCollection: it registers with FPickPacker, resolves through
 * FPickUnpacker, and its entries carry their own staging data -- so a downstream swap node can
 * redirect pick hashes from a source's (GUID, RawIndex) to this collection's, and everything
 * downstream (spawning, fitting) keeps working off the variant entry.
 *
 * The flattened raw-index view is the concatenation of Sources[*].Overrides[*].Entry in
 * declaration order. Unset rows still consume a raw index (they are skipped by iteration and
 * cache build) so flat indices already referenced by emitted pick hashes stay stable when
 * rows are partially authored.
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Variant", meta=(ToolTip = "Per-entry overrides for one or more source collections, for end-of-pipeline asset swapping (biomes, themes)."))
class PCGEXCOLLECTIONS_API UPCGExVariantCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()

public:
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Variant;
	}

	/** Source collections this variant themes; overrides are grouped per source. */
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPCGExVariantSource> Sources;

	/**
	 * Asset-path swap rules -- shorthand over the per-entry rows. Resolved by BuildGroupMapping
	 * against the declared sources; entries already claimed by an explicit row are untouched
	 * (specific beats general). First matching rule wins; duplicate MatchAsset values warn.
	 * Rule payloads occupy the tail of the flattened raw-index view, after all source-group rows.
	 */
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPCGExVariantPathOverride> PathOverrides;

	//~ UPCGExAssetCollection interface -- flattened view over Sources[*].Overrides[*].Entry.
	// InitNumEntries / Sort intentionally keep their base behavior (not-implemented / no-op):
	// entry layout is authored per-source and must not be flattened or reordered externally.
	virtual bool IsValidIndex(int32 InIndex) const override;
	virtual int32 NumEntries() const override;
	virtual void BuildCache() override;
	virtual void ForEachEntry(FForEachConstEntryFunc Iterator) const override;
	virtual void ForEachEntry(FForEachEntryFunc Iterator) override;

	/**
	 * (SourceRawIndex, VariantFlatIndex) pairs for one source group, resolved against the live
	 * (hard-referenced) source: explicit rows via FindRawIndexByEntryId, then asset-path rules over
	 * unclaimed source entries. Pure derivation, no baked state. Orphaned rows warn and are skipped.
	 */
	void BuildGroupMapping(int32 GroupIdx, TArray<FIntPoint>& OutPairs) const;

	/**
	 * Variant-level diagnostics: the 16-bit pick-index ceiling and duplicate asset swap rules.
	 * Not part of BuildGroupMapping -- consumers filter which groups they map (null or unmapped
	 * sources are skipped), so per-group gating would miss whole executions. Call once per execution.
	 */
	void LogFlatViewDiagnostics() const;

	/** Find the override group for a given source collection path. Null if not themed here. */
	const FPCGExVariantSource* FindSourceGroup(const FSoftObjectPath& InSourcePath) const;

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual const FPCGExAssetCollectionEntry* GetEntryAtRawIndex(int32 Index) const override;
	virtual FPCGExAssetCollectionEntry* GetMutableEntryAtRawIndex(int32 Index) override;

	/** Flat raw index -> row payload. O(log groups) via the cached offsets when built. */
	FPCGExAssetCollectionEntry* ResolveRawIndex(int32 Index);
	const FPCGExAssetCollectionEntry* ResolveRawIndex(int32 Index) const;

	/** Flat raw index -> row payload. Sole owner of the traversal: pointer and type can't disagree. */
	const FInstancedStruct* ResolveRawPayload(int32 Index) const;

#if WITH_EDITOR
public:
	/** Rows carry their own payload type; the base `Entries` reflection finds nothing here. */
	virtual const UScriptStruct* EDITOR_GetEntryScriptStruct(int32 RawIndex) const override;

protected:
#endif

	/**
	 * Cached flat-view structure: FlatGroupOffsets[g] = flat start index of Sources[g]'s rows,
	 * with one trailing element = start of the PathOverrides payload tail. FlatTotalEntries < 0
	 * means "not built yet" (fresh object) and lookups fall back to a linear walk.
	 *
	 * CONTRACT: rebuilt on PostLoad / PostEditChangeProperty -- any code
	 * that mutates Sources or PathOverrides structurally OUTSIDE those notification paths must
	 * call RebuildFlatView() itself. Assets are structurally immutable during PCG execution,
	 * which is what makes the unguarded hot-path read safe.
	 */
	TArray<int32> FlatGroupOffsets;
	int32 FlatTotalEntries = INDEX_NONE;

	void RebuildFlatView();
};
