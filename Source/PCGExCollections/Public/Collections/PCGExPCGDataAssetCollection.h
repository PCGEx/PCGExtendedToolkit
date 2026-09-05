// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Core/PCGExCollectionTypeState.h"
#include "Core/PCGExExportSlots.h"
#include "Helpers/PCGExArrayHelpers.h"

#include "PCGExPCGDataAssetCollection.generated.h"

class AActor;
class UPCGDataAsset;
class UPCGExPCGDataAssetCollection;
struct FPCGExLevelExportSource;
class UPCGExLevelDataExporter;
class UPCGExMeshCollection;
class UPCGExActorCollection;
class UPCGExLevelCollection;
class UWorld;
class FObjectPreSaveContext;

UENUM(BlueprintType)
enum class EPCGExDataAssetEntrySource : uint8
{
	DataAsset = 0 UMETA(DisplayName = "Data Asset", ToolTip="Reference an existing PCGDataAsset", ActionIcon="PCGDA_DataAsset"),
	Level     = 1 UMETA(DisplayName = "Level", ToolTip="Export a level to an embedded PCGDataAsset", ActionIcon="PCGDA_Level"),
	Actor     = 2 UMETA(DisplayName = "Actor", ToolTip="Export an actor's attached subtree, as if it were a level, to an embedded PCGDataAsset. Points are relative to the actor.", ActionIcon="PCGDA_Actor"),
};

/**
 * PCGDataAsset collection-level globals. Mirrors UPCGExPCGDataAssetCollection's exporter
 * member 1:1. Editor-only data: in cooked targets the block is empty, querying it is harmless.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] PCGDataAsset Collection Globals")
struct PCGEXCOLLECTIONS_API FPCGExPCGDataAssetCollectionGlobals : public FPCGExCollectionTypeGlobals
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/** Exporter used to convert level-sourced entries into embedded PCGDataAssets during staging.
	 *  If unset, a default exporter is used. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExLevelDataExporter> LevelExporter;
#endif
};

/**
 * PCG data asset collection entry. References a UPCGDataAsset or a subcollection.
 * UpdateStaging() computes combined bounds from all spatial data in the asset.
 *
 * Export-sourced entries also feed the host's shared collection slots by capturing editor-only
 * per-slot snapshots (Captures) during export. The captures are merged into the shared slots by
 * UPCGExPCGDataAssetCollection::CompactSharedFor, which then rewrites each ExportedDataAsset's
 * Tag_EntryIdx attribute on the corresponding pin against the deduplicated shared indices. The
 * CollectionMap pin is rebuilt afterward by RebuildCollectionMapsFor() with every shared + per-entry
 * slot collection registered.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] PCGDataAsset Collection Entry", meta=(ShortName="PCG Data Asset"))
struct PCGEXCOLLECTIONS_API FPCGExPCGDataAssetCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExPCGDataAssetCollectionEntry() = default;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::PCGDataAsset;
	}

	// PCGDataAsset-Specific Properties

	/** Source mode toggle (default = DataAsset for backward compatibility) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	EPCGExDataAssetEntrySource Source = EPCGExDataAssetEntrySource::Level;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="Source == EPCGExDataAssetEntrySource::DataAsset && !bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UPCGDataAsset> DataAsset = nullptr;

	/** Level reference (used when Source == Level) */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="Source == EPCGExDataAssetEntrySource::Level && !bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UWorld> Level;

	/** Root actor whose attached subtree is exported (used when Source == Actor). Its level must not be
	 *  World Partition: a closed partitioned level cannot be read. */
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="Source == EPCGExDataAssetEntrySource::Actor && !bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<AActor> SourceActor;

	/** Only in Level mode. An Actor-sourced entry answers null on purpose: its host level is a container,
	 *  not a level whose content the entry stands for. A DataAsset-sourced entry has no level at all. */
	virtual FSoftObjectPath GetSourceLevelPath() const override
	{
		return (!bIsSubCollection && Source == EPCGExDataAssetEntrySource::Level)
			       ? Level.ToSoftObjectPath()
			       : FSoftObjectPath();
	}

	/** Embedded exported data asset (hidden, serialized, outered to collection in embedded
	 *  mode; null in external mode -- see ExternalExportedDataAsset). */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGDataAsset> ExportedDataAsset;

	/** External-mode mirror of ExportedDataAsset. Populated by the externalization step
	 *  (UPCGExPCGDataAssetCollection::ExternalizeExportedDataAssetsFor). Soft ref so loading the parent
	 *  collection does not pull the on-disk asset; LoadPCGData soft-loads via Staging.Path / CollectionMap. */
	UPROPERTY()
	TSoftObjectPtr<UPCGDataAsset> ExternalExportedDataAsset;

	/** Per-entry (PerEntry-scope) slot collections built by the exporter -- the actor collection, and any
	 *  registered handler's per-entry slot. Each slot's Collection is embedded (an inner of ExportedDataAsset)
	 *  or externalized to its External mirror. Keyed by slot id; no cross-entry mutualization. */
	UPROPERTY()
	TArray<FPCGExExportCollectionSlot> EmbeddedSlots;

#if WITH_EDITORONLY_DATA
	/** Shared-scope captures from this entry's last export, one per slot (meshes, levels, ...). Consumed
	 *  by UPCGExPCGDataAssetCollection::CompactSharedFor as one input to the cross-entry merge.
	 *
	 *  Persisted (NOT Transient) on purpose: when ANY sibling entry rebuilds and shifts shared composition,
	 *  every other entry's Tag_EntryIdx must be rewritten. Storing the captures here lets the rewrite pass
	 *  run without re-walking source levels -- it just reads local picks, applies the new LocalToShared map,
	 *  and writes final hashes. Stripped at cook. */
	UPROPERTY()
	TArray<FPCGExExportSlotCapture> Captures;
#endif

	// ----- Deprecated slots: fixed mesh/level/actor storage became keyed slots. -----
	// UHT registers these under their unsuffixed names with CPF_Deprecated: legacy assets LOAD into them,
	// saves always skip them. OnHostPostLoad moves the values into EmbeddedSlots / Captures.

	UPROPERTY(Instanced)
	TObjectPtr<UPCGExActorCollection> EmbeddedActorCollection_DEPRECATED;

	UPROPERTY()
	TSoftObjectPtr<UPCGExActorCollection> ExternalActorCollection_DEPRECATED;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FPCGExMeshCollectionEntry> EditorMeshContributions_DEPRECATED;

	UPROPERTY()
	TArray<int32> EditorLocalPicks_DEPRECATED;

	UPROPERTY()
	TArray<FPCGExLevelCollectionEntry> EditorLevelContributions_DEPRECATED;

	UPROPERTY()
	TArray<int32> EditorLevelLocalPicks_DEPRECATED;
#endif

	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;
	virtual bool OnHostPostLoad(UPCGExAssetCollection* Host) override;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize() override;
	virtual void EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	virtual FSoftObjectPath EDITOR_GetThumbnailAssetPath() const override;
	virtual FSoftObjectPath EDITOR_GetActivationAssetPath() const override;
#endif

	/** Embedded per-entry slot collection by id, or null. Legacy accessor for the actor slot: FindEmbeddedSlot(Actors). */
	UPCGExAssetCollection* FindEmbeddedCollection(FName SlotId) const;

private:
	/** Level and Actor sources share everything past the load: export into a fresh embedded asset,
	 *  capture contributions, scan sockets. Returns false when the export produced nothing. */
	bool ExportFromSource(const UPCGExAssetCollection* OwningCollection, const FPCGExLevelExportSource& ExportSource);

	/** Editor-only capture buffers -- every reset site in UpdateStaging goes through here. Retires any
	 *  instanced subobjects the captured entries owned. */
	void ResetEditorContributions();

	/** Staging + embedded export back to "nothing" -- a failed or impossible export. */
	void ResetExport(const UPCGExAssetCollection* OwningCollection);
};

/** Concrete collection for UPCGDataAsset references with optional level-sourced entries.
 *
 *  Mutualizes shared-scope slot storage (meshes, nested levels, and any registered handler's shared
 *  slot) across export-sourced entries via the machinery state's SharedSlots: each entry's per-slot
 *  captures are taken during export, then merged per slot (CompactSharedFor). Per-entry
 *  ExportedDataAsset point hashes resolve through the shared collections' CollectionGUIDs at runtime,
 *  eliminating duplicated storage when entries reuse the same meshes, levels or sketches.
 *
 *  The machinery storage + external-storage settings
 *  live on an owned UPCGExPCGDataTypeState (always present, default subobject) and every
 *  lifecycle override is a dispatch into it -- the typed collection is simply a host whose
 *  state is guaranteed, running the exact same code path as an Omni host. Legacy members
 *  migrate into the state on PostLoad (deprecated slots below).
 *
 *  Per-entry slots (actors) are kept on Entry.EmbeddedSlots (no cross-entry merge).
 */

/**
 * Host-agnostic view over the state the PCGDataAsset collection machinery operates on
 * (shared-slot compaction, collection maps, externalization). The typed collection composes it
 * from its own state (MakeMachinery); heterogeneous hosts compose it from their per-type state.
 *
 * SharedSlots POINTS TO the host state's storage so the cores mutate the real refs.
 * Entries is the PCGData-typed LEAF payload view in host order -- external-asset naming uses
 * the view index, which matches the raw entry index on typed collections.
 */
struct PCGEXCOLLECTIONS_API FPCGExPCGDataAssetMachinery
{
	UPCGExAssetCollection* Host = nullptr; // Outer for generated subobjects; GUID / package identity
	TArray<FPCGExPCGDataAssetCollectionEntry*> Entries;

	TArray<FPCGExExportCollectionSlot>* SharedSlots = nullptr;

	bool bExternalActive = false;
	FString ExportFolderPath;
	FString ExternalAssetPrefix;

	bool IsValid() const
	{
		return Host && SharedSlots;
	}
};

/**
 * Keep-buffers for the external-mode save scrub (UPCGExPCGDataAssetCollection::Scrub*ForSave /
 * Restore*AfterSave). A scrub/restore pair brackets ONE Serialize call on one thread, so the
 * raw slot/entry pointers never see a GC window or an array mutation in between. The scrubbed
 * ref SETS live in the cores -- typed collection and type state share them by construction.
 */
struct PCGEXCOLLECTIONS_API FPCGExPCGDataSharedScrubKeep
{
	TArray<FPCGExExportCollectionSlot>* Slots = nullptr;
	TArray<TObjectPtr<UPCGExAssetCollection>> Kept;
};

struct PCGEXCOLLECTIONS_API FPCGExPCGDataEntryScrubKeep
{
	TArray<FPCGExPCGDataAssetCollectionEntry*> Entries;
	TArray<TObjectPtr<UPCGDataAsset>> Data;
	TArray<TArray<TObjectPtr<UPCGExAssetCollection>>> Embedded;

	void Reset()
	{
		Entries.Reset();
		Data.Reset();
		Embedded.Reset();
	}
};

/**
 * Machinery state/processor for PCGDataAsset-typed entries hosted OUTSIDE a native
 * PCGDataAsset collection. Owns the same shared/external
 * storage the typed collection keeps -- keyed collection slots -- and dispatches the
 * host-agnostic machinery cores against it from the host lifecycle hooks. With this state
 * present, level-sourced entries in an Omni behave like they do in a native collection
 * (export, compaction, collection maps, externalization).
 */
UCLASS(DisplayName="PCG Data Asset Machinery")
class PCGEXCOLLECTIONS_API UPCGExPCGDataTypeState : public UPCGExCollectionTypeState
{
	GENERATED_BODY()

public:
	/** Mirrors UPCGExPCGDataAssetCollection::bUseExternalAssets. */
	UPROPERTY(EditAnywhere, Category = "External Storage")
	bool bUseExternalAssets = false;

	/** Mirrors UPCGExPCGDataAssetCollection::ExportFolder. */
	UPROPERTY(EditAnywhere, Category = "External Storage", meta=(EditCondition="bUseExternalAssets", ContentDir, LongPackageName))
	FDirectoryPath ExportFolder;

	/** Shared-scope slot collections (meshes, levels, and any registered handler's shared slot), keyed
	 *  by slot id. Each Collection is an embedded working buffer outered to the host, or externalized
	 *  to its External mirror. */
	UPROPERTY()
	TArray<FPCGExExportCollectionSlot> SharedSlots;

	// ----- Deprecated slots: the fixed mesh/level members became keyed SharedSlots. -----
	// Legacy data loads into these (CPF_Deprecated: tagged-property name match, saves skip); PostLoad
	// adopts them into SharedSlots once.

	UPROPERTY(Instanced)
	TObjectPtr<UPCGExMeshCollection> SharedMeshCollection_DEPRECATED;

	UPROPERTY(Instanced)
	TObjectPtr<UPCGExLevelCollection> SharedLevelCollection_DEPRECATED;

	UPROPERTY()
	TSoftObjectPtr<UPCGExMeshCollection> ExternalSharedMeshCollection_DEPRECATED;

	UPROPERTY()
	TSoftObjectPtr<UPCGExLevelCollection> ExternalSharedLevelCollection_DEPRECATED;

	bool IsExternalActive() const
	{
		return bUseExternalAssets && !ExportFolder.Path.IsEmpty();
	}

	FPCGExExportCollectionSlot* FindSharedSlot(const FName SlotId)
	{
		return PCGExExportSlots::Find(SharedSlots, SlotId);
	}

	const FPCGExExportCollectionSlot* FindSharedSlot(const FName SlotId) const
	{
		return PCGExExportSlots::Find(SharedSlots, SlotId);
	}

	/**
	 * Adopt a legacy fixed-member pair into the keyed slot. Idempotent and order-agnostic: both the
	 * host's PostLoad (pre-C1 members) and this object's PostLoad (C1 members) funnel through here, and
	 * whichever runs first wins nothing -- a slot already holding data is left alone.
	 */
	void AdoptLegacySlot(FName SlotId, UPCGExAssetCollection* Collection, const FSoftObjectPath& External);

	/**
	 * Compose the machinery view: HOST identity (outer/GUID) + THIS state's storage + the
	 * host's PCGDataAsset-typed entry payloads in host order. External-asset naming uses the
	 * view index -- stable as long as row order is, same contract as the typed collection.
	 */
	FPCGExPCGDataAssetMachinery MakeMachinery(UPCGExAssetCollection* Host);

	//~ UPCGExCollectionTypeState
	virtual void OnHostPreSave(UPCGExAssetCollection* Host, FObjectPreSaveContext SaveContext) override;
	virtual void OnHostPostDuplicate(UPCGExAssetCollection* Host, bool bDuplicateForPIE) override;
	virtual void OnHostSerializeSave_Begin(UPCGExAssetCollection* Host) override;
	virtual void OnHostSerializeSave_End(UPCGExAssetCollection* Host) override;
#if WITH_EDITOR
	virtual void EDITOR_OnHostPostStagingRebuild(UPCGExAssetCollection* Host) override;

	/** Embedded exports moved with the host; their Staging.Path strings and the CollectionMap rows baked
	 *  inside them did not. Re-stamps both from the live objects. */
	virtual void EDITOR_OnHostRelocated(UPCGExAssetCollection* Host) override;
	virtual void AppendCookDependencyAssetPaths(const UPCGExAssetCollection* Host, TSet<FSoftObjectPath>& OutPaths) const override;
	virtual void EDITOR_AppendExternalPackages(const UPCGExAssetCollection* Host, TSet<UPackage*>& OutPackages) const override;

	/** External-storage toggle reactions -- the engine delivers property edits to the
	 *  instanced state FIRST, then walks up to the host (whose base PostEditChangeProperty
	 *  triggers the staging rebuild). Without this, External -> Embedded would never
	 *  internalize the externalized assets. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Fresh states adopt the seed source's external-storage SETTINGS. Shared collections /
	 *  External* soft refs are NOT copied -- session
	 *  buffers and the source's own external packages respectively. */
	virtual void OnAddedToHost(UPCGExAssetCollection* Host, const UPCGExAssetCollection* SeedSource) override;

	/** Warns when the ignored source's external-storage settings differ from this state's
	 *  (first-creator-wins is otherwise invisible). */
	virtual void OnSeedSourceIgnored(UPCGExAssetCollection* Host, const UPCGExAssetCollection* SeedSource) override;

	/** Warns when removal orphans externalized packages (never deletes them). */
	virtual void OnRemovedFromHost(UPCGExAssetCollection* Host) override;
#endif

	/** Own-member scrub: in external mode the shared collections are session working buffers;
	 *  their instanced refs must not bake hard references into the saved package. Entry-level
	 *  refs live in HOST data and are scrubbed by the OnHostSerializeSave pair instead. */
	virtual void Serialize(FArchive& Ar) override;

	/** Legacy fixed members -> SharedSlots. */
	virtual void PostLoad() override;

private:
	/** OnHostSerializeSave_Begin/End restore buffer -- see FPCGExPCGDataEntryScrubKeep. */
	FPCGExPCGDataEntryScrubKeep ScrubKeep;
};

UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | PCGDataAsset", meta=(ToolTip = "A weighted collection of PCG Data Assets.", PCGExNodeLibraryDoc="staging/collections/pcg-data-asset-collection"))
class PCGEXCOLLECTIONS_API UPCGExPCGDataAssetCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	friend struct FPCGExPCGDataAssetCollectionEntry;

public:
	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::PCGDataAsset;
	}

	// Settings

#if WITH_EDITORONLY_DATA
	/** Exporter used to convert level-sourced entries into embedded PCGDataAssets during staging.
	 *  If unset, a default exporter is used. Instanced so custom exporters can expose their own settings.
	 *  Editor-only: level harvesting is an authoring operation; cooked data carries the baked result. */
	UPROPERTY(EditAnywhere, Instanced, Category = Settings)
	TObjectPtr<UPCGExLevelDataExporter> LevelExporter;
#endif

protected:
	virtual bool GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const override;

public:
	UPCGExPCGDataAssetCollection();

	/**
	 * Owned machinery state: external-storage settings
	 * (bUseExternalAssets / ExportFolder) plus the shared/external collection storage the
	 * PCGDataAsset machinery operates on. Always present (default subobject); the same
	 * state class an Omni host instantiates per present PCGData entry type. Shared
	 * subobjects it references stay outered to THIS collection (host package), exactly like
	 * on an Omni host.
	 */
	UPROPERTY(EditAnywhere, Instanced, NoClear, Category = "External Storage", meta=(DisplayName="External Storage"))
	TObjectPtr<UPCGExPCGDataTypeState> MachineryState;

	// Entries Array

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExPCGDataAssetCollectionEntry> Entries;

	PCGEX_ASSET_COLLECTION_BODY(FPCGExPCGDataAssetCollectionEntry)

public:
	UPCGExPCGDataTypeState* GetMachineryState() const
	{
		return MachineryState;
	}

	/** Machinery-state accessor through the generic host seam (see base). */
	using UPCGExAssetCollection::FindTypeState; // keep the base template visible
	virtual UPCGExCollectionTypeState* FindTypeState(const UClass* StateClass) const override
	{
		return (MachineryState && StateClass && MachineryState->GetClass()->IsChildOf(StateClass))
			       ? MachineryState.Get()
			       : nullptr;
	}

private:
	// ----- Deprecated slots -----
	// UHT registers these under their unsuffixed names with CPF_Deprecated: legacy assets
	// LOAD into them (tagged-property name match), saves always skip them. PostLoad moves
	// the values into MachineryState and clears them. Remove after a deprecation cycle.

	UPROPERTY()
	bool bUseExternalAssets_DEPRECATED = false;

	UPROPERTY()
	FDirectoryPath ExportFolder_DEPRECATED;

	UPROPERTY(Instanced)
	TObjectPtr<UPCGExMeshCollection> SharedMeshCollection_DEPRECATED;

	UPROPERTY(Instanced)
	TObjectPtr<UPCGExLevelCollection> SharedLevelCollection_DEPRECATED;

	UPROPERTY()
	TSoftObjectPtr<UPCGExMeshCollection> ExternalSharedMeshCollection_DEPRECATED;

	UPROPERTY()
	TSoftObjectPtr<UPCGExLevelCollection> ExternalSharedLevelCollection_DEPRECATED;

public:
	/**
	 * Manual convenience: recompact every shared slot from each entry's captured editor-only
	 * contributions, rewrite per-entry Tag_EntryIdx against the resulting shared indices, and
	 * rebuild every entry's CollectionMap pin. Idempotent.
	 * The automatic paths (post-staging rebuild, cook-time PreSave net, PostDuplicate
	 * re-stamp) do NOT route through this -- they dispatch through MachineryState's
	 * lifecycle hooks directly; this exists for explicit tooling-driven recompaction.
	 */
	void RebuildSharedCollections();

	// Host-agnostic machinery cores. Each operates purely on the given state view, so any
	// host that can compose a FPCGExPCGDataAssetMachinery can run them. The private instance
	// methods below are thin wrappers over these. All editor-only in effect: bodies guard on WITH_EDITOR(_DATA).

	/** Every registered Shared-scope slot: merge captures, rewrite the slot pin's Tag_EntryIdx. */
	static void CompactSharedFor(FPCGExPCGDataAssetMachinery& State);
	static void RebuildCollectionMapsFor(FPCGExPCGDataAssetMachinery& State);
	/** Shared slots -> <Prefix>_<SlotId>; per-entry slots -> <Prefix>_E%03d_<SlotId>. */
	static void ExternalizeSlotCollectionsFor(FPCGExPCGDataAssetMachinery& State);
	static void ExternalizeExportedDataAssetsFor(FPCGExPCGDataAssetMachinery& State);
	static void InternalizeSubobjectsFor(FPCGExPCGDataAssetMachinery& State);
	static void SaveExternalPackagesFor(FPCGExPCGDataAssetMachinery& State);

	/** Shared package-collection walk over the machinery storage: the loaded packages the
	 *  shared slots and per-entry exports currently live in, minus the host's own and
	 *  the transient package (so embedded mode contributes nothing). Drives both the manual
	 *  SaveExternalPackages utility and the editor-save coordination seam. */
	static void CollectExternalPackagesFor(
		const UPCGExAssetCollection* Host,
		const TArray<FPCGExExportCollectionSlot>& InSharedSlots,
		const TArray<const FPCGExPCGDataAssetCollectionEntry*>& InEntries,
		TSet<UPackage*>& OutPackages);

	/** Orchestrator: compaction -> shared/per-entry externalization -> collection maps -> data
	 *  asset externalization, in the order the soft-path baking requires. */
	static void RebuildSharedCollectionsFor(FPCGExPCGDataAssetMachinery& State);

	/** Per-host prefix used to derive external asset names. The host's GUID makes it stable
	 *  across rebuilds (P4-friendly overwrites) and unique across collections sharing an
	 *  ExportFolder. SINGLE source of the format -- typed collection and type state both
	 *  compose their machinery through this. */
	static FString MakeExternalAssetPrefixFor(const UPCGExAssetCollection* Host);

	/** External-mode save-scrub cores (see Serialize). Scrub* records the slots and current
	 *  values into the keep-buffer and nulls the live refs; Restore* writes them back and
	 *  resets the buffer. The pair must bracket exactly one Serialize call on one thread.
	 *  Shared refs = every shared slot collection; entry refs = each entry's ExportedDataAsset +
	 *  every embedded slot collection. */
	static void ScrubSharedRefsForSave(TArray<FPCGExExportCollectionSlot>& Slots, FPCGExPCGDataSharedScrubKeep& OutKeep);
	static void RestoreSharedRefsAfterSave(FPCGExPCGDataSharedScrubKeep& Keep);
	static void ScrubEntryRefsForSave(const TArray<FPCGExPCGDataAssetCollectionEntry*>& InEntries, FPCGExPCGDataEntryScrubKeep& OutKeep);
	static void RestoreEntryRefsAfterSave(FPCGExPCGDataEntryScrubKeep& Keep);

#if WITH_EDITOR
	/** Shared cook-dependency walk over the machinery storage (typed host and type state
	 *  drive this with their own members / entry views). Parameters are explicit because the
	 *  callers are const contexts and the machinery view is a mutation view. See
	 *  GetCookDependencyAssetPaths for the embedded-vs-external rationale per block. */
	static void AppendCookDependencyAssetPathsFor(
		const TArray<FPCGExExportCollectionSlot>& InSharedSlots,
		const TArray<const FPCGExPCGDataAssetCollectionEntry*>& InEntries,
		TSet<FSoftObjectPath>& OutPaths);
#endif

	/**
	 * True when Host runs the PCGDataAsset collection machinery (compaction, collection maps,
	 * externalization). Level-sourced entries stage nothing in hosts that don't -- this is
	 * the capability query behind that guard, so future host kinds only change THIS.
	 */
	static bool HostSupportsDataAssetMachinery(const UPCGExAssetCollection* Host);

	// Lifecycle -- every override below is a dispatch into MachineryState (the state runs
	// the same host-agnostic cores an Omni host drives; see UPCGExPCGDataTypeState).

	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

#if WITH_EDITOR
	virtual void EDITOR_OnPostStagingRebuild() override;
	virtual void EDITOR_RunTypeStatesPostStaging() override;
	virtual void EDITOR_OnHostRelocated() override;
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;

	/** IPCGExExternalPackageProducer via the owned machinery state. */
	virtual void EDITOR_GetExternalPackages(TSet<UPackage*>& OutPackages) const override;

	/**
	 * Cook-path override -- adds the references that GetAssetPaths intentionally omits
	 * (those are reserved for runtime cherry-picking). Walks embedded shared / per-entry
	 * slot collections so their leaf soft refs reach the cook, and surfaces the
	 * externalized-package soft paths so their on-disk assets cook too.
	 *
	 * Assumes external assets exist on disk from a prior editor save -- the normal
	 * workflow (toggle external, save, commit). Re-running PreSave at cook time
	 * overwrites them with current content but doesn't change which paths cook.
	 */
	virtual void GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
#endif

private:
	/** Manual utility: editor-save every external package this collection produced.
	 *  Deliberately NOT called from PreSave -- see the cook rationale in
	 *  UPCGExPCGDataTypeState::OnHostPreSave. */
	void SaveExternalPackages();
};
