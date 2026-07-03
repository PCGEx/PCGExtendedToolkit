// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Core/PCGExAssetCollection.h"
#include "Helpers/PCGExArrayHelpers.h"

#include "PCGExPCGDataAssetCollection.generated.h"

class UPCGDataAsset;
class UPCGExPCGDataAssetCollection;
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
};

/**
 * PCG data asset collection entry. References a UPCGDataAsset or a subcollection.
 * UpdateStaging() computes combined bounds from all spatial data in the asset.
 *
 * Level-sourced entries also feed the parent collection's SharedMeshCollection and
 * SharedLevelCollection by capturing editor-only snapshots (EditorMeshContributions +
 * EditorLocalPicks, EditorLevelContributions + EditorLevelLocalPicks) during export.
 * The captured snapshots are merged into the shared collections by
 * UPCGExPCGDataAssetCollection::CompactSharedMesh / CompactSharedLevel, which then
 * rewrite each ExportedDataAsset's Tag_EntryIdx attribute on the corresponding pin
 * against the deduplicated shared indices. The CollectionMap pin is rebuilt afterward
 * by RebuildCollectionMaps() with all shared + per-entry collections registered.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] PCGDataAsset Collection Entry")
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

	/** Embedded exported data asset (hidden, serialized, outered to collection in embedded
	 *  mode; null in external mode -- see ExternalExportedDataAsset). */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGDataAsset> ExportedDataAsset;

	/** Embedded actor collection built by level exporter when bGenerateCollections is enabled.
	 *  Actor classes are kept per-entry (no cross-entry mutualization). Null in external mode. */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGExActorCollection> EmbeddedActorCollection;

	/** External-mode mirrors of ExportedDataAsset / EmbeddedActorCollection. Populated by the
	 *  externalization step (UPCGExPCGDataAssetCollection::ExternalizeExportedDataAssets /
	 *  ExternalizeSharedAndActorCollections). Soft refs so loading the parent collection does
	 *  not pull these on-disk assets; LoadPCGData soft-loads via Staging.Path / CollectionMap. */
	UPROPERTY()
	TSoftObjectPtr<UPCGDataAsset> ExternalExportedDataAsset;

	UPROPERTY()
	TSoftObjectPtr<UPCGExActorCollection> ExternalActorCollection;

#if WITH_EDITORONLY_DATA
	/** Snapshot of mesh entries captured from this entry's source level. Editor-only,
	 *  stripped at cook. Consumed by UPCGExPCGDataAssetCollection::CompactSharedMesh
	 *  as one input to the cross-entry shared-mesh merge. */
	UPROPERTY()
	TArray<FPCGExMeshCollectionEntry> EditorMeshContributions;

	/** Caller-computed "common-ancestor" inherited-defaults view for this export's contributing
	 *  actors -- i.e., per property, the value the actors would resolve if they had no per-instance
	 *  override. When all unique BP classes agree at the CDO level, that's the CDO value; when
	 *  they disagree, the asset's authored default fills in. Transient -- recomputed on every
	 *  export from the actors in this entry's source level. Consumed by CompactSharedMesh to
	 *  derive the shared MeshCollection's CollectionProperties as a per-property union across all
	 *  contributing entries' aggregates. */
	UPROPERTY(Transient)
	TArray<FInstancedStruct> EditorMeshInheritedDefaults;

	/** Per-mesh-point packed local selection, parallel to ExportedDataAsset's "Meshes" pin.
	 *  Layout: low 16 bits = local entry index into EditorMeshContributions,
	 *  high 16 bits = secondary index + 1 (0 = no variant; matches FPickPacker convention).
	 *  Sentinel value -1 means "no valid pick" (point gets no Tag_EntryIdx hash).
	 *
	 *  Persisted (NOT Transient) on purpose: when ANY sibling entry rebuilds and shifts shared
	 *  composition, every other entry's Tag_EntryIdx must be rewritten. Storing local picks here
	 *  lets the rewrite pass run without re-walking source levels -- it just reads local picks,
	 *  applies the new LocalToShared map, and writes final hashes. Stripped at cook. */
	UPROPERTY()
	TArray<int32> EditorLocalPicks;

	/** Snapshot of level entries (nested level instances) captured from this entry's source
	 *  level. Editor-only, stripped at cook. Consumed by
	 *  UPCGExPCGDataAssetCollection::CompactSharedLevel as one input to the cross-entry
	 *  shared-level merge. */
	UPROPERTY()
	TArray<FPCGExLevelCollectionEntry> EditorLevelContributions;

	/** Per-level-point packed local selection, parallel to ExportedDataAsset's "Levels" pin.
	 *  Layout: identity packing of the local entry index into EditorLevelContributions
	 *  (no secondary dimension on levels yet). Sentinel -1 means "no valid pick".
	 *
	 *  Persisted for the same reason as EditorLocalPicks: when sibling-entry composition
	 *  shifts SharedLevelCollection indices, the rewrite pass reads local picks and applies
	 *  the new LocalToShared map without re-walking source levels. Stripped at cook. */
	UPROPERTY()
	TArray<int32> EditorLevelLocalPicks;
#endif

	// Lifecycle

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize() override;
	virtual void EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
	virtual FSoftObjectPath EDITOR_GetThumbnailAssetPath() const override;
#endif
};

/** Concrete collection for UPCGDataAsset references with optional level-sourced entries.
 *
 *  Mutualizes mesh + nested-level storage across level-sourced entries via SharedMeshCollection
 *  and SharedLevelCollection: each entry's per-level snapshots (EditorMeshContributions,
 *  EditorLevelContributions) are captured during export, then merged into the two shared
 *  collections (CompactSharedMesh, CompactSharedLevel). Per-entry ExportedDataAsset point
 *  hashes resolve through the shared collections' CollectionGUIDs at runtime, eliminating
 *  duplicated storage when entries reuse the same meshes or the same nested levels.
 *
 *  Actor classes are kept per-entry on Entry.EmbeddedActorCollection (no cross-entry merge).
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | PCGDataAsset", meta=(ToolTip = "A weighted collection of PCG Data Assets."))
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

	/** Externalize generated subobjects (ExportedDataAsset, EmbeddedActorCollection,
	 *  SharedMeshCollection, SharedLevelCollection) as separate uassets so the collection's
	 *  load footprint is small and per-asset loads are demand-driven by LoadPCGData. The
	 *  externalized uassets are managed by the rebuild pipeline; overwritten on every rebuild;
	 *  not meant to be hand-edited. */
	UPROPERTY(EditAnywhere, Category = "External Storage")
	bool bUseExternalAssets = false;

	/** Content folder where externalized assets are written. Must be set when bUseExternalAssets
	 *  is on; if empty, externalization is skipped with a warning and the collection falls back
	 *  to embedded mode for that rebuild. Names are derived from the collection's asset name
	 *  and entry index, so files are reused across rebuilds (P4-friendly overwrites). */
	UPROPERTY(EditAnywhere, Category = "External Storage",
		meta=(EditCondition="bUseExternalAssets", ContentDir, LongPackageName))
	FDirectoryPath ExportFolder;

	// Entries Array

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExPCGDataAssetCollectionEntry> Entries;

	/** Shared mesh collection. Built by CompactSharedMesh() as the deduplicated union of all
	 *  level-sourced entries' EditorMeshContributions. Outered to the collection in embedded
	 *  mode, persists for the lifetime of the asset, and carries the stable CollectionGUID
	 *  that gets baked into every per-entry ExportedDataAsset's "Meshes"-pin Tag_EntryIdx
	 *  hashes. Null in external mode -- see ExternalSharedMeshCollection. */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGExMeshCollection> SharedMeshCollection;

	/** Shared level collection. Built by CompactSharedLevel() as the deduplicated union of all
	 *  level-sourced entries' EditorLevelContributions (nested level instances). Same lifetime
	 *  + GUID-stability guarantee as SharedMeshCollection. Null in external mode. */
	UPROPERTY(Instanced)
	TObjectPtr<UPCGExLevelCollection> SharedLevelCollection;

	/** External-mode mirrors. Populated by ExternalizeSharedAndActorCollections; consumed at
	 *  runtime indirectly via the CollectionMap pin's soft paths. Loading the parent collection
	 *  does not pull these. */
	UPROPERTY()
	TSoftObjectPtr<UPCGExMeshCollection> ExternalSharedMeshCollection;

	UPROPERTY()
	TSoftObjectPtr<UPCGExLevelCollection> ExternalSharedLevelCollection;

	PCGEX_ASSET_COLLECTION_BODY(FPCGExPCGDataAssetCollectionEntry)

public:
	/**
	 * Recompact both shared collections (mesh + level) from each entry's captured editor-only
	 * contributions, rewrite per-entry Tag_EntryIdx on the Meshes and Levels pins against the
	 * resulting shared indices, and rebuild every entry's CollectionMap pin with the two
	 * shared collections + the entry's EmbeddedActorCollection registered.
	 *
	 * Idempotent. Triggered automatically:
	 *   - After every editor-driven staging rebuild via EDITOR_OnPostStagingRebuild.
	 *   - At cook time via PreSave as a safety net.
	 *   - After PostDuplicate to re-stamp hashes against the duplicate's freshly-generated
	 *     shared-collection CollectionGUIDs.
	 */
	void RebuildSharedCollections();

	// Lifecycle

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

#if WITH_EDITOR
	virtual void EDITOR_OnPostStagingRebuild() override;
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/**
	 * Cook-path override -- adds the references that GetAssetPaths intentionally omits
	 * (those are reserved for runtime cherry-picking). Walks embedded shared / actor
	 * subcollections so their leaf soft refs reach the cook, and surfaces the
	 * externalized-package soft paths so their on-disk assets cook too.
	 *
	 * Assumes external assets exist on disk from a prior editor save -- the normal
	 * workflow (toggle external, save, commit). Re-running PreSave at cook time
	 * overwrites them with current content but doesn't change which paths cook.
	 */
	virtual void GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const override;
#endif

private:
	void CompactSharedMesh();
	void CompactSharedLevel();
	void RebuildCollectionMaps();

	/** True when external storage is both requested and configurable. ExportFolder is required
	 *  in external mode; an empty folder silently falls back to embedded for that rebuild. */
	bool IsExternalActive() const
	{
		return bUseExternalAssets && !ExportFolder.Path.IsEmpty();
	}

	/** Per-collection prefix used to derive external asset names. The collection's GUID makes
	 *  it stable across rebuilds (P4-friendly overwrites) and unique across collections that
	 *  share an ExportFolder. */
	FString GetExternalAssetPrefix() const
	{
		return FString::Printf(TEXT("G_%08X"), GetCollectionGUID());
	}

	/** External-storage helpers. Each is a no-op unless IsExternalActive(). Externalize* must
	 *  run in the right order around RebuildCollectionMaps so the baked CollectionMap soft
	 *  paths reflect the final external locations -- see RebuildSharedCollections. */
	void ExternalizeSharedAndActorCollections();
	void ExternalizeExportedDataAssets();
	void InternalizeSubobjects();
	void SaveExternalPackages();
};
