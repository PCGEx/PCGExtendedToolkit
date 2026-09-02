// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Core/PCGExExportSlots.h"
#include "UObject/Object.h"

#include "PCGExLevelDataExporter.generated.h"

class AActor;
class UPCGDataAsset;
class UWorld;

/**
 * What a level export reads: a world, the actors to consider, and the frame the output is expressed
 * in. Two shapes -- a whole persistent level (identity frame, today's behaviour) or one actor's
 * attached subtree exported as if it were a level (frame = the root's transform, root excluded).
 * The exporter still runs its content filter over Actors; the source only decides candidacy.
 */
struct PCGEXCOLLECTIONS_API FPCGExLevelExportSource
{
	UWorld* World = nullptr;

	/** Null for a whole-level source. Never exported itself. */
	AActor* Root = nullptr;

	/** Every written transform is expressed relative to this. */
	FTransform Frame = FTransform::Identity;

	TArray<AActor*> Actors;

	bool IsValid() const
	{
		return World != nullptr;
	}

	/** Every actor of the persistent level, identity frame. */
	static FPCGExLevelExportSource FromWorld(UWorld* InWorld);

	/**
	 * Root's attached descendants, recursively, frame = root's actor transform. When Root implements
	 * IPCGExAssemblyRoot its prune predicate bounds the descent and its frame is used instead.
	 */
	static FPCGExLevelExportSource FromActorSubtree(AActor* InRoot);

	/** World-space to frame-relative. Identity frame returns the input unchanged. */
	FTransform ToFrame(const FTransform& WorldTransform) const
	{
		return Frame.Equals(FTransform::Identity) ? WorldTransform : WorldTransform.GetRelativeTransform(Frame);
	}
};

/**
 * Output state populated by UPCGExLevelDataExporter::ExportLevelData (rich C++ overload).
 *
 * Assign the storage pointers before calling the 3-arg ExportLevelData. The exporter never builds
 * shared collections, never writes Tag_EntryIdx for shared slots and never emplaces the CollectionMap
 * pin -- it hands shared-slot contributions back as captures (one per slot) and leaves final hashing +
 * CollectionMap emission to the caller. Per-entry slots are built into Embedded collections here,
 * reusing the previous object found under the same slot id (its CollectionGUID and EntryIds survive).
 *
 * LocalPicks layout (one int32 per point on the slot's pin):
 *   low 16 bits  = local entry index into the capture's Entries
 *   high 16 bits = secondary index + 1 (0 = no variant; matches FPickPacker hash convention)
 *   value == -1  = sentinel, no valid pick for this point
 * Slots without secondaries pack (local, -1), i.e. the high half is 0.
 *
 * Consumed by UPCGExPCGDataAssetCollection's shared-collection rebuild to merge captures across
 * sibling entries and rewrite Tag_EntryIdx hashes against the deduplicated collections.
 */
struct PCGEXCOLLECTIONS_API FPCGExLevelExportContext
{
	/** Shared-scope hand-off, one capture per slot. Null = shared contributions are not captured. */
	TArray<FPCGExExportSlotCapture>* Captures = nullptr;

	/** Per-entry slots: previous embedded collections in, rebuilt ones out. Null = built but not handed back. */
	TArray<FPCGExExportCollectionSlot>* EmbeddedSlots = nullptr;

	/** Owner the captures are serialized under (the host collection). Instanced subobjects inside
	 *  captured entries are outered here so they never cross packages when the export is externalized.
	 *  Null falls back to the exported asset. */
	UObject* CaptureOuter = nullptr;

	/** Pack a (local entry index, secondary index) pair into the int32 stored in LocalPicks. */
	static FORCEINLINE int32 PackLocalPick(int32 LocalEntryIdx, int16 SecondaryIdx)
	{
		const int32 SecPlus1 = (static_cast<int32>(SecondaryIdx) + 1) & 0xFFFF;
		return (SecPlus1 << 16) | (LocalEntryIdx & 0xFFFF);
	}

	/** Unpack a LocalPicks value back into local entry index and secondary index. */
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
 *  - C++ virtual ExportLevelData(Source, OutAsset, FPCGExLevelExportContext&):
 *    used by UPCGExPCGDataAssetCollection to capture editor-only per-slot
 *    contributions that feed shared-collection compaction (CompactSharedFor).
 *    The exporter never builds shared collections, never writes their Tag_EntryIdx,
 *    and never emplaces the CollectionMap pin -- those responsibilities live on the
 *    caller. Default impl on the base forwards to the BP-facing path with the
 *    source's world -- a BP exporter always sees the whole level, never a subtree.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, meta=(DisplayName="Level Data Exporter", PCGExNodeLibraryDoc="staging/collections/pcg-data-asset-collection/level-data-exporter"))
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
	 * per-slot entry captures and packed local picks for the consumer to merge across
	 * sibling entries. Every transform written must go through Source.ToFrame -- a
	 * subtree source is only a level when its output is root-relative.
	 *
	 * Default implementation forwards to the BP-facing ExportLevelData; nothing is
	 * captured in that path. Override in C++ subclasses (see UPCGExDefaultLevelDataExporter)
	 * to populate the context.
	 */
	virtual bool ExportLevelData(const FPCGExLevelExportSource& Source, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext)
	{
		return ExportLevelData(Source.World, OutAsset);
	}
};
