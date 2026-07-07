// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Object.h"

#include "PCGExLevelDataExporter.generated.h"

class UPCGDataAsset;
class UPCGExActorCollection;
struct FPCGExMeshCollectionEntry;
struct FPCGExLevelCollectionEntry;

/**
 * Output state populated by UPCGExLevelDataExporter::ExportLevelData (rich C++ overload).
 *
 * Assign the output pointers before calling the 3-arg ExportLevelData. The exporter never
 * builds inline UPCGExMeshCollection / UPCGExLevelCollection and never writes Tag_EntryIdx
 * or the CollectionMap pin itself -- it captures contributions through these pointers and
 * leaves final hashing + CollectionMap emission to the caller.
 *
 * MeshLocalPicks layout (one int32 per "Meshes" pin point):
 *   low 16 bits  = local entry index into MeshContributions
 *   high 16 bits = secondary index + 1 (0 = no variant; matches FPickPacker hash convention)
 *   value == -1  = sentinel, no valid pick for this point
 *
 * LevelLocalPicks layout (one int32 per "Levels" pin point):
 *   value        = local entry index into LevelContributions (no secondary on levels yet)
 *   value == -1  = sentinel, no valid pick for this point
 *
 * Consumed by UPCGExPCGDataAssetCollection's shared-collection rebuild to merge contributions
 * across sibling entries and rewrite Tag_EntryIdx hashes against the deduplicated collections.
 */
struct PCGEXCOLLECTIONS_API FPCGExLevelExportContext
{
	TArray<FPCGExMeshCollectionEntry>* MeshContributions = nullptr;
	TArray<int32>* MeshLocalPicks = nullptr;

	TArray<FPCGExLevelCollectionEntry>* LevelContributions = nullptr;
	TArray<int32>* LevelLocalPicks = nullptr;

	/** Receives the per-entry actor collection built inline by the exporter. Lets the caller
	 *  pick it up directly instead of walking the asset's inner-object graph. */
	TObjectPtr<UPCGExActorCollection>* ActorCollectionOut = nullptr;

	/** Previous per-entry actor collection, when one exists. Reused as the working buffer
	 *  (re-outered to the new asset) so its CollectionGUID and EntryIds survive re-exports.
	 *  The caller keeps it GC-reachable for the duration of the call. */
	UPCGExActorCollection* PreviousActorCollection = nullptr;

	/** Receives the "common-ancestor" inherited-defaults view computed from the contributing
	 *  actors' BP class chains -- per property, the value all unique classes agree on at the
	 *  CDO level, or the asset's authored default when classes disagree. Used by the shared
	 *  MeshCollection rebuild to set CollectionProperties without falling back to whichever
	 *  per-instance override was iterated first. Optional: when null, the caller has no opinion
	 *  and the merge falls through to per-entry contributors. */
	TArray<FInstancedStruct>* MeshInheritedDefaults = nullptr;

	/** Pack a (local entry index, secondary index) pair into the int32 stored in MeshLocalPicks. */
	static FORCEINLINE int32 PackLocalPick(int32 LocalEntryIdx, int16 SecondaryIdx)
	{
		const int32 SecPlus1 = (static_cast<int32>(SecondaryIdx) + 1) & 0xFFFF;
		return (SecPlus1 << 16) | (LocalEntryIdx & 0xFFFF);
	}

	/** Unpack a MeshLocalPicks value back into local entry index and secondary index. */
	static FORCEINLINE void UnpackLocalPick(int32 Packed, int32& OutLocalIdx, int16& OutSecondary)
	{
		OutLocalIdx = Packed & 0xFFFF;
		OutSecondary = static_cast<int16>(((Packed >> 16) & 0xFFFF) - 1);
	}
};

/**
 * Abstract base class for level → PCG data asset conversion.
 * Subclass in C++ or Blueprint to customize how a level's actors are
 * exported into a UPCGDataAsset during collection staging.
 *
 * Instanced on the collection via Instanced/EditInlineNew so that
 * derived classes can expose custom UPROPERTYs (filtering, transform
 * adjustments, etc.) directly in the collection's details panel.
 *
 * Two API surfaces:
 *  - BlueprintNativeEvent ExportLevelData(World, OutAsset): BP-facing, simple.
 *    Custom BP subclasses override _Implementation. No contribution capture; the
 *    resulting asset carries raw attributes (Mesh / ActorClass / LevelAsset) and no
 *    Tag_EntryIdx / CollectionMap. Standalone use only.
 *  - C++ virtual ExportLevelData(World, OutAsset, FPCGExLevelExportContext&):
 *    used by UPCGExPCGDataAssetCollection to capture editor-only mesh + level
 *    contributions that feed shared-collection compaction (CompactSharedMesh /
 *    CompactSharedLevel). The exporter never builds inline embedded collections,
 *    never writes Tag_EntryIdx, and never emplaces the CollectionMap pin --
 *    those responsibilities live on the caller. Default impl on the base forwards
 *    to the BP-facing path.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class PCGEXCOLLECTIONS_API UPCGExLevelDataExporter : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Export level data from the given world into the target data asset.
	 * The asset's TaggedData is already cleared before this is called.
	 *
	 * @param World      The loaded world to extract data from.
	 * @param OutAsset   The target data asset to populate. Outered to the owning collection.
	 * @return true if export succeeded and the asset contains valid data.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|LevelExport")
	bool ExportLevelData(UWorld* World, UPCGDataAsset* OutAsset);

	virtual bool ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset)
	{
		return false;
	}

	/**
	 * Rich C++-only export entry point. Populates an FPCGExLevelExportContext with
	 * mesh-entry contributions and per-mesh-point packed local picks for the consumer
	 * to merge across sibling entries.
	 *
	 * Default implementation forwards to the BP-facing ExportLevelData; mesh
	 * contributions are not captured in that path. Override in C++ subclasses
	 * (see UPCGExDefaultLevelDataExporter) to populate the context.
	 */
	virtual bool ExportLevelData(UWorld* World, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext)
	{
		return ExportLevelData(World, OutAsset);
	}
};
