// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExPCGDataAssetCollection.h"

#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

#include "PCGDataAsset.h"
#include "PCGExCollectionsSettingsCache.h"
#include "PCGExLog.h"
#include "PCGExSchemaMerging.h"
#include "PCGExSocketProvider.h"
#include "PCGParamData.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Engine/Level.h"
#include "Helpers/PCGExActorHelpers.h"
#include "Helpers/PCGExCollectionExternalization.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExLevelDataExporter.h"
#include "Helpers/PCGExLevelExportHandler.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "Core/PCGExCollectionHelpers.h"
#include "GameFramework/Actor.h"


// Static-init type registration: TypeId=PCGDataAsset, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(PCGDataAsset, UPCGExPCGDataAssetCollection, FPCGExPCGDataAssetCollectionEntry, "PCG Data Asset Collection", Base)

// Machinery registration: TypeId -> globals block + state class. Colocated with the type;
// third-party types with machinery register theirs the same way, in their own module.
namespace PCGExPCGDataAssetCollection
{
	struct FMachineryRegistration
	{
		FMachineryRegistration()
		{
			PCGExAssetCollection::FTypeRegistry::AddPendingCustomization(
				PCGExAssetCollection::TypeIds::PCGDataAsset,
				[](PCGExAssetCollection::FTypeInfo& Info)
				{
					Info.GlobalsStruct = FPCGExPCGDataAssetCollectionGlobals::StaticStruct();
					// Hosts carrying this state run the PCGData shared compaction /
					// collection maps / externalization for their PCGData entries.
					Info.StateClass = UPCGExPCGDataTypeState::StaticClass();
				});
		}
	};

	FMachineryRegistration GMachineryRegistration;
}

#pragma region FPCGExPCGDataAssetCollectionEntry

bool FPCGExPCGDataAssetCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection && ParentCollection->bDoNotIgnoreInvalidEntries)
	{
		switch (Source)
		{
		case EPCGExDataAssetEntrySource::DataAsset:
			if (!DataAsset.ToSoftObjectPath().IsValid())
			{
				return false;
			}
			break;
		case EPCGExDataAssetEntrySource::Level:
			if (!Level.ToSoftObjectPath().IsValid())
			{
				return false;
			}
			break;
		case EPCGExDataAssetEntrySource::Actor:
			if (!SourceActor.ToSoftObjectPath().IsValid())
			{
				return false;
			}
			break;
		default:
			ensureMsgf(false, TEXT("Unhandled EPCGExDataAssetEntrySource"));
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

namespace PCGExPCGDataAssetCollectionInternal
{
	/** Compute combined bounds from all spatial data in a PCGDataAsset. */
	static FBox ComputeBoundsFromAsset(const UPCGDataAsset* Asset)
	{
		FBox CombinedBounds(ForceInit);
		if (Asset)
		{
			for (const FPCGTaggedData& TaggedData : Asset->Data.GetAllInputs())
			{
				if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data))
				{
					CombinedBounds += SpatialData->GetBounds();
				}
			}
		}
		return CombinedBounds.IsValid ? CombinedBounds : FBox(ForceInit);
	}
}

void FPCGExPCGDataAssetCollectionEntry::ResetEditorContributions()
{
#if WITH_EDITORONLY_DATA
#if WITH_EDITOR
	// Captured entries may own instanced subobjects (handler payloads) outered to the host; an
	// unreferenced inner would otherwise linger under it until the next save's traversal.
	for (FPCGExExportSlotCapture& Capture : Captures)
	{
		for (FInstancedStruct& Entry : Capture.Entries)
		{
			if (Entry.IsValid())
			{
				PCGExCollectionHelpers::RetireInstancedSubobjects(Entry.GetScriptStruct(), Entry.GetMutableMemory());
			}
		}
	}
#endif
	Captures.Reset();
#endif
}

void FPCGExPCGDataAssetCollectionEntry::ResetExport(const UPCGExAssetCollection* OwningCollection)
{
	// Own subobjects get the transient-rename so they stop serializing; a pointer to another
	// package's private subobject (cross-asset copy) is only nulled -- it must never survive a save.
	auto Discard = [OwningCollection](auto& Embedded)
	{
		if (!Embedded)
		{
			return;
		}
		if (OwningCollection && Embedded->IsIn(OwningCollection))
		{
			Embedded->Rename(nullptr, GetTransientPackage(),
			                 REN_DontCreateRedirectors | REN_NonTransactional);
		}
		Embedded = nullptr;
	};
	Discard(ExportedDataAsset);
	for (FPCGExExportCollectionSlot& Slot : EmbeddedSlots)
	{
		Discard(Slot.Collection);
	}

	Staging.Bounds = FBox(ForceInit);
	Staging.Path = FSoftObjectPath();
	ResetEditorContributions();
}

UPCGExAssetCollection* FPCGExPCGDataAssetCollectionEntry::FindEmbeddedCollection(const FName SlotId) const
{
	const FPCGExExportCollectionSlot* Slot = PCGExExportSlots::Find(EmbeddedSlots, SlotId);
	return Slot ? Slot->Collection.Get() : nullptr;
}

bool FPCGExPCGDataAssetCollectionEntry::OnHostPostLoad(UPCGExAssetCollection* Host)
{
	bool bRewritten = FPCGExAssetCollectionEntry::OnHostPostLoad(Host);

	// Fixed per-entry actor storage -> the "Actors" embedded slot. An already-populated slot wins.
	if (EmbeddedActorCollection_DEPRECATED || !ExternalActorCollection_DEPRECATED.IsNull())
	{
		FPCGExExportCollectionSlot& Slot = PCGExExportSlots::FindOrAdd(EmbeddedSlots, PCGExLevelExport::Slots::Actors);
		if (!Slot.Collection)
		{
			Slot.Collection = EmbeddedActorCollection_DEPRECATED;
		}
		if (Slot.External.IsNull())
		{
			Slot.External = TSoftObjectPtr<UPCGExAssetCollection>(ExternalActorCollection_DEPRECATED.ToSoftObjectPath());
		}
		EmbeddedActorCollection_DEPRECATED = nullptr;
		ExternalActorCollection_DEPRECATED.Reset();
		bRewritten = true;
	}

#if WITH_EDITORONLY_DATA
	// Fixed mesh/level captures -> keyed captures. Migrated, never dropped: a partial rebuild after the
	// upgrade recompacts from THESE, and a missing capture would shrink the shared collection and stale
	// every sibling's hashes. Level picks were identity ints, which is exactly PackLocalPick(local, -1).
	if (!EditorMeshContributions_DEPRECATED.IsEmpty() || !EditorLocalPicks_DEPRECATED.IsEmpty())
	{
		FPCGExExportSlotCapture& Capture = PCGExExportSlots::FindOrAdd(Captures, PCGExLevelExport::Slots::Meshes);
		if (Capture.Entries.IsEmpty() && Capture.LocalPicks.IsEmpty())
		{
			Capture.Entries.Reserve(EditorMeshContributions_DEPRECATED.Num());
			for (const FPCGExMeshCollectionEntry& Entry : EditorMeshContributions_DEPRECATED)
			{
				Capture.Entries.AddDefaulted_GetRef().InitializeAs<FPCGExMeshCollectionEntry>(Entry);
			}
			Capture.LocalPicks = MoveTemp(EditorLocalPicks_DEPRECATED);
		}
		EditorMeshContributions_DEPRECATED.Reset();
		EditorLocalPicks_DEPRECATED.Reset();
		bRewritten = true;
	}

	if (!EditorLevelContributions_DEPRECATED.IsEmpty() || !EditorLevelLocalPicks_DEPRECATED.IsEmpty())
	{
		FPCGExExportSlotCapture& Capture = PCGExExportSlots::FindOrAdd(Captures, PCGExLevelExport::Slots::Levels);
		if (Capture.Entries.IsEmpty() && Capture.LocalPicks.IsEmpty())
		{
			Capture.Entries.Reserve(EditorLevelContributions_DEPRECATED.Num());
			for (const FPCGExLevelCollectionEntry& Entry : EditorLevelContributions_DEPRECATED)
			{
				Capture.Entries.AddDefaulted_GetRef().InitializeAs<FPCGExLevelCollectionEntry>(Entry);
			}
			Capture.LocalPicks = MoveTemp(EditorLevelLocalPicks_DEPRECATED);
		}
		EditorLevelContributions_DEPRECATED.Reset();
		EditorLevelLocalPicks_DEPRECATED.Reset();
		bRewritten = true;
	}
#endif

	return bRewritten;
}

// Shared by the Level and Actor sources once their world is loaded and transform-current.
//
// Routes through the 3-arg exporter API so per-slot captures land directly in the entry's UPROPERTY
// storage and per-entry slot collections are rebuilt in place. Shared-slot Tag_EntryIdx hashes and the
// CollectionMap pin are NOT written here -- those are produced by the host's CompactSharedFor /
// RebuildCollectionMapsFor, which see every entry's captures and resolve final shared indices.
bool FPCGExPCGDataAssetCollectionEntry::ExportFromSource(const UPCGExAssetCollection* OwningCollection, const FPCGExLevelExportSource& ExportSource)
{
	// Always recreate ExportedDataAsset fresh. Reusing + resetting TaggedData leaves orphaned
	// UPCGBasePointData subobjects in the outer chain that still serialize into the .uasset,
	// which causes save-time pointer traversal crashes after repeated rebuilds.
	if (ExportedDataAsset)
	{
		ExportedDataAsset->Rename(nullptr, GetTransientPackage(),
		                          REN_DontCreateRedirectors | REN_NonTransactional);
	}
	ExportedDataAsset = NewObject<UPCGDataAsset>(const_cast<UPCGExAssetCollection*>(OwningCollection));

	// Exporter via the type-globals seam, else a transient default. Editor-only data:
	// cooked builds always take the fallback.
	UPCGExLevelDataExporter* Exporter = nullptr;
#if WITH_EDITORONLY_DATA
	if (OwningCollection)
	{
		FPCGExPCGDataAssetCollectionGlobals Globals;
		if (OwningCollection->GetTypeGlobals(Globals))
		{
			Exporter = Globals.LevelExporter;
		}
	}
#endif

	TObjectPtr<UPCGExLevelDataExporter> FallbackExporter;
	if (!Exporter)
	{
		const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;
		UClass* ExporterClass = Settings.DefaultLevelExporterClass
			? Settings.DefaultLevelExporterClass.Get()
			: UPCGExDefaultLevelDataExporter::StaticClass();
#if PCGEX_ENGINE_VERSION < 507
		FallbackExporter = NewObject<UPCGExLevelDataExporter>(GetTransientPackage(), ExporterClass);
#else
		FallbackExporter = NewObject<UPCGExLevelDataExporter>(GetTransientPackageAsObject(), ExporterClass);
#endif

		Exporter = FallbackExporter;
	}

	// Wire the export context to write directly into the entry's UPROPERTY storage -- no copy at the
	// API boundary. Captures exist in editor builds only; shipping builds run the exporter without
	// capturing (the shared collections are already baked into the per-entry hashes at cook time).
	FPCGExLevelExportContext ExportContext;
	// Reset the capture buffers so a failed export doesn't leave stale data from a prior rebuild
	// contributing to the shared compaction.
	ResetEditorContributions();
#if WITH_EDITORONLY_DATA
	ExportContext.Captures = &Captures;
#endif
	ExportContext.CaptureOuter = const_cast<UPCGExAssetCollection*>(OwningCollection);

	// The previous per-entry collections are the exporter's working buffers -- their CollectionGUIDs
	// and EntryIds are bound by external references (variants). Cold external sessions load the
	// externalized assets back. The slot refs stay assigned during export so the objects stay
	// GC-reachable.
	for (FPCGExExportCollectionSlot& Slot : EmbeddedSlots)
	{
		if (!Slot.Collection && !Slot.External.IsNull())
		{
			PCGExHelpers::LoadBlocking_AnyThreadTpl(Slot.External);
			Slot.Collection = Slot.External.Get();
		}
	}
	ExportContext.EmbeddedSlots = &EmbeddedSlots;

	const bool bSuccess = Exporter->ExportLevelData(ExportSource, ExportedDataAsset, ExportContext);

	if (bSuccess)
	{
		// A slot the exporter did not rebuild this time (no content for it anymore) still points at an
		// object that was retired with the previous exported asset -- drop it.
		for (FPCGExExportCollectionSlot& Slot : EmbeddedSlots)
		{
			if (Slot.Collection && !Slot.Collection->IsIn(ExportedDataAsset))
			{
				Slot.Collection = nullptr;
			}
		}

		Staging.Path = FSoftObjectPath(ExportedDataAsset);
		Staging.Bounds = PCGExPCGDataAssetCollectionInternal::ComputeBoundsFromAsset(ExportedDataAsset);

		// Same actor set and frame as the export: a socket provider inside a subtree is a
		// socket of that subtree, expressed root-relative like every other point.
		for (AActor* Actor : ExportSource.Actors)
		{
			if (IPCGExSocketProvider* Provider = Cast<IPCGExSocketProvider>(Actor))
			{
				FPCGExSocket& NewSocket = Staging.Sockets.Emplace_GetRef(
					Provider->GetSocketName_Implementation(),
					ExportSource.ToFrame(Provider->GetSocketTransform_Implementation()),
					Provider->GetSocketTag_Implementation());
				NewSocket.bManaged = true;
			}
		}
	}
	else
	{
		Staging.Path = FSoftObjectPath();
		Staging.Bounds = FBox(ForceInit);

		// Failed exports return before any slot is rebuilt; the previous collections' outer chain was
		// just retired -- never serialize them.
		for (FPCGExExportCollectionSlot& Slot : EmbeddedSlots)
		{
			Slot.Collection = nullptr;
		}
		ResetEditorContributions();
	}

	return bSuccess;
}

// Loads the PCG data asset, or exports a level / an actor subtree into an embedded one, and
// computes combined bounds. See ExportFromSource for the export half.
void FPCGExPCGDataAssetCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	if (bIsSubCollection)
	{
		ClearManagedSockets();
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	switch (Source)
	{
	case EPCGExDataAssetEntrySource::DataAsset:
	{
		ClearManagedSockets();
		Staging.Path = DataAsset.ToSoftObjectPath();
		TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThreadTpl(DataAsset);

		if (const UPCGDataAsset* Asset = DataAsset.Get())
		{
			Staging.Bounds = PCGExPCGDataAssetCollectionInternal::ComputeBoundsFromAsset(Asset);
		}
		else
		{
			Staging.Bounds = FBox(ForceInit);
		}

		// DataAsset-sourced entries don't contribute to the shared collections -- clear any
		// stale contributions left behind by a prior export-sourced rebuild.
		ResetEditorContributions();

		PCGExHelpers::SafeReleaseHandle(Handle);
		break;
	}
	case EPCGExDataAssetEntrySource::Level:
	case EPCGExDataAssetEntrySource::Actor:
	{
		const bool bActorSource = Source == EPCGExDataAssetEntrySource::Actor;
		const FSoftObjectPath SourcePath = bActorSource ? SourceActor.ToSoftObjectPath() : Level.ToSoftObjectPath();

		// Export depends on machinery the host must run -- in hosts that don't, stage nothing
		// and point at the composition path. Capability query so future host kinds (per-type
		// processor seam) only change the helper.
		if (!UPCGExPCGDataAssetCollection::HostSupportsDataAssetMachinery(OwningCollection))
		{
			UE_LOG(LogPCGEx, Warning,
			       TEXT("%s-sourced PCGDataAsset entry ('%s') is hosted by a collection without the level-export machinery -- entry skipped. Author it in a PCGDataAsset collection and reference that collection as a subcollection entry instead."),
			       bActorSource ? TEXT("Actor") : TEXT("Level"), *SourcePath.ToString());
			ClearManagedSockets();
			ResetExport(OwningCollection);
			break;
		}

		if (bActorSource)
		{
			TSharedPtr<FStreamableHandle> Handle;
			FString Failure;
			AActor* Root = PCGExCollections::ResolveLevelActor(SourcePath, Handle, &Failure);
			if (!Root)
			{
				// An unreachable source keeps the last export, sockets included: the actor is usually
				// unreachable only because its level is closed, and an empty module is worse than a
				// stale one. A deleted actor is the author's to fix -- the warning names it.
				UE_LOG(LogPCGEx, Warning,
				       TEXT("Actor-sourced PCGDataAsset entry: %s -- %s."),
				       *Failure, ExportedDataAsset ? TEXT("keeping the previous export") : TEXT("no previous export, entry stages nothing"));
				if (ExportedDataAsset)
				{
					Staging.Path = FSoftObjectPath(ExportedDataAsset);
				}
				else
				{
					ClearManagedSockets();
					ResetExport(OwningCollection);
				}
				break;
			}

			ClearManagedSockets();
			ExportFromSource(OwningCollection, FPCGExLevelExportSource::FromActorSubtree(Root));
			PCGExHelpers::SafeReleaseHandle(Handle);
			break;
		}

		ClearManagedSockets();
		TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(SourcePath);
		UWorld* LoadedWorld = Level.Get();
		if (!LoadedWorld)
		{
			ResetExport(OwningCollection);
			PCGExHelpers::SafeReleaseHandle(Handle);
			break;
		}

		// Asset-loaded worlds carry identity ComponentToWorld/Bounds until this runs -- the
		// exporter, the bounds evaluators and the socket scan all read live component state.
		PCGExHelpers::EnsureWorldTransformsCurrent(LoadedWorld);
		ExportFromSource(OwningCollection, FPCGExLevelExportSource::FromWorld(LoadedWorld));
		PCGExHelpers::SafeReleaseHandle(Handle);
		break;
	}
	default:
		ensureMsgf(false, TEXT("Unhandled EPCGExDataAssetEntrySource"));
		ClearManagedSockets();
		ResetExport(OwningCollection);
		break;
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
}

void FPCGExPCGDataAssetCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);

	switch (Source)
	{
	case EPCGExDataAssetEntrySource::DataAsset:
		DataAsset = TSoftObjectPtr<UPCGDataAsset>(InPath);
		break;
	case EPCGExDataAssetEntrySource::Level:
		Level = TSoftObjectPtr<UWorld>(InPath);
		break;
	case EPCGExDataAssetEntrySource::Actor:
		SourceActor = TSoftObjectPtr<AActor>(InPath);
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled EPCGExDataAssetEntrySource"));
		break;
	}
}

#if WITH_EDITOR
void FPCGExPCGDataAssetCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	// Only the export-backed sources carry an embedded asset.
	if (Source != EPCGExDataAssetEntrySource::Level && Source != EPCGExDataAssetEntrySource::Actor)
	{
		ExportedDataAsset = nullptr;
		for (FPCGExExportCollectionSlot& Slot : EmbeddedSlots)
		{
			Slot.Collection = nullptr;
		}
		ResetEditorContributions();
	}
}

void FPCGExPCGDataAssetCollectionEntry::EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	if (bIsSubCollection)
	{
		return;
	}

	// Source refs trigger rebuild -- not Staging.Path, which for the export-backed sources
	// points at an embedded ExportedDataAsset inside the collection's own package. An actor
	// path's package is its level's, so the package-name match fires on a level save; OFPA
	// actor saves reach it through the editor module's outer-path remap.
	FSoftObjectPath SourcePath;
	switch (Source)
	{
	case EPCGExDataAssetEntrySource::DataAsset:
		SourcePath = DataAsset.ToSoftObjectPath();
		break;
	case EPCGExDataAssetEntrySource::Level:
		SourcePath = Level.ToSoftObjectPath();
		break;
	case EPCGExDataAssetEntrySource::Actor:
		SourcePath = SourceActor.ToSoftObjectPath();
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled EPCGExDataAssetEntrySource"));
		break;
	}
	if (SourcePath.IsValid())
	{
		OutPaths.Emplace(SourcePath);
	}
}

FSoftObjectPath FPCGExPCGDataAssetCollectionEntry::EDITOR_GetThumbnailAssetPath() const
{
	if (bIsSubCollection)
	{
		return FPCGExAssetCollectionEntry::EDITOR_GetThumbnailAssetPath();
	}

	// Export-backed entries draw their EXPORT: the data-asset thumbnail renderer shows the exported
	// geometry, which is what the entry stands for. Live embedded object first, externalized asset
	// next, the source as the fallback before any export exists.
	switch (Source)
	{
	case EPCGExDataAssetEntrySource::DataAsset:
		return DataAsset.ToSoftObjectPath();
	case EPCGExDataAssetEntrySource::Level:
	case EPCGExDataAssetEntrySource::Actor:
		if (ExportedDataAsset)
		{
			return FSoftObjectPath(ExportedDataAsset);
		}
		if (!ExternalExportedDataAsset.IsNull())
		{
			return ExternalExportedDataAsset.ToSoftObjectPath();
		}
		return EDITOR_GetActivationAssetPath();
	default:
		ensureMsgf(false, TEXT("Unhandled EPCGExDataAssetEntrySource"));
		return FSoftObjectPath();
	}
}

FSoftObjectPath FPCGExPCGDataAssetCollectionEntry::EDITOR_GetActivationAssetPath() const
{
	if (bIsSubCollection)
	{
		return FPCGExAssetCollectionEntry::EDITOR_GetActivationAssetPath();
	}

	// Opening an export means opening what authored it: the level, or the actor's level.
	switch (Source)
	{
	case EPCGExDataAssetEntrySource::DataAsset:
		return DataAsset.ToSoftObjectPath();
	case EPCGExDataAssetEntrySource::Level:
		return Level.ToSoftObjectPath();
	case EPCGExDataAssetEntrySource::Actor:
		return SourceActor.ToSoftObjectPath().GetWithoutSubPath();
	default:
		ensureMsgf(false, TEXT("Unhandled EPCGExDataAssetEntrySource"));
		return FSoftObjectPath();
	}
}
#endif

#pragma endregion

#pragma region SharedCompact internals

namespace PCGExSharedCompact
{
	UPCGBasePointData* FindPointDataByPin(UPCGDataAsset* Asset, FName PinName)
	{
		if (!Asset)
		{
			return nullptr;
		}
		for (FPCGTaggedData& TD : Asset->Data.TaggedData)
		{
			if (TD.Pin == PinName)
			{
				if (UPCGBasePointData* PD = const_cast<UPCGBasePointData*>(Cast<UPCGBasePointData>(TD.Data)))
				{
					return PD;
				}
			}
		}
		return nullptr;
	}

	// ExternalizeUObject + Internalize live in Helpers/PCGExCollectionExternalization.h now
	// (shared with the Valency bonding-rules externalization). See that header for contracts.

#if WITH_EDITOR
	const FPCGExExportSlotCapture* FindCapture(const FPCGExPCGDataAssetCollectionEntry& Entry, const FName SlotId)
	{
		return PCGExExportSlots::Find(Entry.Captures, SlotId);
	}

	// Merge every entry's captured contributions for one slot into its deduplicated shared collection
	// (identity via the policy's Hash + Equals), then rewrite Tag_EntryIdx on the slot's pin against the
	// resulting shared indices. Tags/Category on existing shared entries are preserved across rebuilds
	// when identity survives. Deterministic ordering: by content-derived SortKey ascending -- stable
	// across cold cooks and editor restarts (does NOT depend on the per-process FName hash).
	void CompactSharedSlot(
		UObject* Outer,
		const TArray<FPCGExPCGDataAssetCollectionEntry*>& Entries,
		const FPCGExExportSlotDesc& Desc,
		const IPCGExExportSlotPolicy& Policy,
		FPCGExExportCollectionSlot& Slot,
		const bool bExternalActive)
	{
		const UScriptStruct* EntryStruct = Desc.EntryStruct;

		// Skip everything when there's nothing to merge AND no existing shared state to clear.
		// Avoids a synchronous external-asset load on unrelated edits (e.g. weight tweak on a
		// non-level entry) when no entry contributes to this slot.
		bool bHasContributions = false;
		for (const FPCGExPCGDataAssetCollectionEntry* E : Entries)
		{
			const FPCGExExportSlotCapture* Capture = FindCapture(*E, Desc.SlotId);
			if (Capture && Capture->Entries.Num() > 0)
			{
				bHasContributions = true;
				break;
			}
		}
		if (!bHasContributions && !Slot.Collection && (!bExternalActive || Slot.External.IsNull()))
		{
			return;
		}

		// External mode: if the in-memory ref was nulled by a prior externalization, pull the
		// asset back into the collection package as the working buffer so the preserved-fields
		// pass below sees the previous entries' user edits (Tags/Category). The next
		// ExternalizeSlotCollectionsFor will overwrite the same external uasset.
		if (!Slot.Collection && bExternalActive && !Slot.External.IsNull())
		{
			if (UPCGExAssetCollection* Loaded = Slot.External.LoadSynchronous())
			{
				Loaded->Rename(nullptr, Outer, REN_DontCreateRedirectors | REN_NonTransactional);
				Slot.Collection = Loaded;
			}
		}

		// A slot whose registered class changed cannot keep the object (its rows are another struct).
		if (Slot.Collection && Slot.Collection->GetClass() != Desc.CollectionClass)
		{
			UE_LOG(LogPCGEx, Warning, TEXT("Shared slot '%s' held a '%s' but the slot is now registered as '%s'; rebuilding from scratch (external references to it are lost)."),
			       *Desc.SlotId.ToString(), *Slot.Collection->GetClass()->GetName(), *Desc.CollectionClass->GetName());
			Slot.Collection->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Slot.Collection = nullptr;
		}

		// Reuse the same UObject across calls so its CollectionGUID -- baked into every
		// per-entry Tag_EntryIdx -- stays stable. Replacing it would invalidate on-disk hashes.
		if (!Slot.Collection)
		{
			Slot.Collection = NewObject<UPCGExAssetCollection>(Outer, Desc.CollectionClass);
		}
		UPCGExAssetCollection* SharedCollection = Slot.Collection;

		struct FPreserved
		{
			FInstancedStruct Identity;
			TSet<FName> Tags;
			FName Category = NAME_None;
			int32 EntryId = 0;
			bool bEntryIdConsumed = false; // exact-matched by a merged group; keeps its id out of the loose-fallback bank
		};
		TMap<uint32, TArray<FPreserved>> PreservedByHash;
		SharedCollection->ForEachEntry([&](const FPCGExAssetCollectionEntry* E, int32)
		{
			if (!E)
			{
				return;
			}
			const uint32 H = Policy.Hash(*E);
			FPreserved& P = PreservedByHash.FindOrAdd(H).AddDefaulted_GetRef();
			P.Identity.InitializeAs(EntryStruct, reinterpret_cast<const uint8*>(E));
			P.Tags = E->Tags;
			P.Category = E->Category;
			P.EntryId = E->EntryId;
		});

		struct FGroup
		{
			uint32 Hash = 0;
			const FPCGExAssetCollectionEntry* Representative = nullptr;
			FString SortKey;
			TArray<TPair<int32, int32>> Contributors; // (entryIdx, localContribIdx)
			int32 WeightSum = 0;
		};
		TMap<uint32, TArray<FGroup>> HashBuckets;

		for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); EntryIdx++)
		{
			const FPCGExExportSlotCapture* Capture = FindCapture(*Entries[EntryIdx], Desc.SlotId);
			if (!Capture)
			{
				continue;
			}
			for (int32 LocalIdx = 0; LocalIdx < Capture->Entries.Num(); LocalIdx++)
			{
				const FInstancedStruct& Instanced = Capture->Entries[LocalIdx];
				if (Instanced.GetScriptStruct() != EntryStruct)
				{
					// A capture written by a different registration of this slot id; it cannot be merged.
					continue;
				}
				const FPCGExAssetCollectionEntry& Contrib = *Instanced.GetPtr<FPCGExAssetCollectionEntry>();
				const uint32 H = Policy.Hash(Contrib);

				TArray<FGroup>& Bucket = HashBuckets.FindOrAdd(H);
				FGroup* Match = nullptr;
				for (FGroup& G : Bucket)
				{
					if (Policy.Equals(*G.Representative, Contrib))
					{
						Match = &G;
						break;
					}
				}
				if (!Match)
				{
					FGroup NewGroup;
					NewGroup.Hash = H;
					NewGroup.Representative = &Contrib;
					NewGroup.SortKey = Policy.SortKey(Contrib);
					Match = &Bucket.Add_GetRef(MoveTemp(NewGroup));
				}
				Match->Contributors.Emplace(EntryIdx, LocalIdx);
				Match->WeightSum += FMath::Max(0, Contrib.Weight);
			}
		}

		TArray<FGroup> AllGroups;
		for (auto& Pair : HashBuckets)
		{
			for (FGroup& G : Pair.Value)
			{
				AllGroups.Add(MoveTemp(G));
			}
		}
		// Order purely by the content-derived, process-stable SortKey. FGroup::Hash is NOT used here:
		// GetTypeHash(FSoftObjectPath) rides the per-process FName comparison index and reshuffles across
		// sessions/cooks. The policy's SortKey fully discriminates every distinct group, so there is no
		// tie-break to fall back on. Hash is retained only as an in-process bucket key.
		AllGroups.Sort([](const FGroup& A, const FGroup& B)
		{
			return A.SortKey < B.SortKey;
		});

		TArray<FInstancedStruct> MergedEntries;
		MergedEntries.Reserve(AllGroups.Num());

		TArray<TArray<int32>> LocalToSharedByEntry;
		LocalToSharedByEntry.SetNum(Entries.Num());
		for (int32 i = 0; i < Entries.Num(); i++)
		{
			// -1 = no shared mapping; rewrite pass leaves the hash unwritten.
			const FPCGExExportSlotCapture* Capture = FindCapture(*Entries[i], Desc.SlotId);
			LocalToSharedByEntry[i].Init(-1, Capture ? Capture->Entries.Num() : 0);
		}

		for (int32 SharedIdx = 0; SharedIdx < AllGroups.Num(); SharedIdx++)
		{
			const FGroup& G = AllGroups[SharedIdx];
			FInstancedStruct& MergedInstanced = MergedEntries.AddDefaulted_GetRef();
			MergedInstanced.InitializeAs(EntryStruct, reinterpret_cast<const uint8*>(G.Representative));
			FPCGExAssetCollectionEntry& Merged = *MergedInstanced.GetMutablePtr<FPCGExAssetCollectionEntry>();
			Merged.Weight = FMath::Max(1, G.WeightSum);

			// Contribution snapshots carry their SOURCE's id, not this collection's; zero is
			// also the "not preserved" marker for the loose pass below.
			Merged.EntryId = 0;

			if (TArray<FPreserved>* Bucket = PreservedByHash.Find(G.Hash))
			{
				for (FPreserved& P : *Bucket)
				{
					if (Policy.Equals(*P.Identity.GetPtr<FPCGExAssetCollectionEntry>(), *G.Representative))
					{
						Merged.Tags = P.Tags;
						Merged.Category = P.Category;
						// PropertyOverrides is intentionally NOT preserved: it's derived from
						// per-export contributions, not user-authored on the shared collection.
						// Tags/Category ARE user-authored and have no per-export contributor.

						// EntryId IS preserved: external references bind by id.
						Merged.EntryId = P.EntryId;
						P.bEntryIdConsumed = true;
						break;
					}
				}
			}

			for (const TPair<int32, int32>& C : G.Contributors)
			{
				LocalToSharedByEntry[C.Key][C.Value] = SharedIdx;
			}
		}

		// Loose EntryId fallback: content-changed entries re-claim the previous id bound to
		// the same primary asset. Claim-once in SortKey order (deterministic); anything still
		// 0 gets a fresh id from the SyncEntryIds pass below.
		{
			PCGExAssetCollection::FEntryIdBank FallbackIds;
			for (TPair<uint32, TArray<FPreserved>>& Pair : PreservedByHash)
			{
				for (const FPreserved& P : Pair.Value)
				{
					if (!P.bEntryIdConsumed)
					{
						FallbackIds.Deposit(0, GetTypeHash(Policy.PrimaryPath(*P.Identity.GetPtr<FPCGExAssetCollectionEntry>())), P.EntryId);
					}
				}
			}

			for (FInstancedStruct& MergedInstanced : MergedEntries)
			{
				FPCGExAssetCollectionEntry& Merged = *MergedInstanced.GetMutablePtr<FPCGExAssetCollectionEntry>();
				if (Merged.EntryId == 0)
				{
					Merged.EntryId = FallbackIds.ClaimLoose(GetTypeHash(Policy.PrimaryPath(Merged)));
				}
			}
		}

		// Previous rows are overwritten wholesale; retire the subobjects they owned first, then write the
		// merged rows with their own duplicates (a capture keeps its originals under the host).
		SharedCollection->ForEachEntry([EntryStruct](FPCGExAssetCollectionEntry* E, int32)
		{
			if (E)
			{
				PCGExCollectionHelpers::RetireInstancedSubobjects(EntryStruct, E);
			}
		});
		SharedCollection->InitNumEntries(MergedEntries.Num());
		for (int32 i = 0; i < MergedEntries.Num(); i++)
		{
			FPCGExAssetCollectionEntry* Dst = SharedCollection->GetMutableEntryRaw(i);
			EntryStruct->CopyScriptStruct(Dst, MergedEntries[i].GetMemory());
			PCGExCollectionHelpers::DuplicateInstancedSubobjects(EntryStruct, Dst, SharedCollection);
		}

		// Aggregate the per-export "inherited defaults" views across every contributing entry.
		// Per property name, only values unanimously agreed across entries survive; disagreements
		// drop out and fall through to per-entry contributors at merge time. Slots that capture no
		// view produce an empty aggregate.
		TArray<TConstArrayView<FInstancedStruct>> InheritedViews;
		InheritedViews.Reserve(Entries.Num());
		for (const FPCGExPCGDataAssetCollectionEntry* E : Entries)
		{
			if (const FPCGExExportSlotCapture* Capture = FindCapture(*E, Desc.SlotId))
			{
				InheritedViews.Emplace(Capture->InheritedDefaults);
			}
		}
		TArray<FInstancedStruct> InheritedDefaultsAggregate = PCGExProperties::AggregateAgreedValuesByName(InheritedViews);

		// Rebuild CollectionProperties from the union of every merged entry's enabled
		// PropertyOverrides, and re-sync the per-entry overrides against it. No-op for
		// collection types whose entries don't carry property data.
		SharedCollection->RefreshCollectionPropertiesFromEntries(
			EPCGExSchemaMergePolicy::StrictTypeMatch,
			InheritedDefaultsAggregate);

		SharedCollection->RebuildStagingData(true);

		// Per-entry Tag_EntryIdx rewrite. CollectionMap is rebuilt separately so it can
		// register every other slot too. Sequential: UPCGMetadata mutation is not safe on
		// worker threads in editor flow.
		PCGExCollections::FPickPacker Packer;
		Packer.RegisterCollection(SharedCollection);

		for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); EntryIdx++)
		{
			FPCGExPCGDataAssetCollectionEntry& Entry = *Entries[EntryIdx];
			const FPCGExExportSlotCapture* Capture = FindCapture(Entry, Desc.SlotId);
			if (!Entry.ExportedDataAsset || !Capture)
			{
				continue;
			}

			UPCGBasePointData* PD = FindPointDataByPin(Entry.ExportedDataAsset, Desc.PinName);
			if (!PD)
			{
				continue;
			}
			UPCGMetadata* Meta = PD->MutableMetadata();
			if (!Meta)
			{
				continue;
			}

			TPCGValueRange<int64> MetaEntries = PD->GetMetadataEntryValueRange();
			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->FindOrCreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, 0, false, true);
			if (!EntryHashAttr)
			{
				continue;
			}

			const TArray<int32>& LocalToShared = LocalToSharedByEntry[EntryIdx];
			const TArray<int32>& LocalPicks = Capture->LocalPicks;
			const int32 N = FMath::Min(LocalPicks.Num(), MetaEntries.Num());
			for (int32 i = 0; i < N; i++)
			{
				const int32 Packed = LocalPicks[i];
				if (Packed == -1)
				{
					continue;
				}
				int32 LocalIdx;
				int16 Sec;
				FPCGExLevelExportContext::UnpackLocalPick(Packed, LocalIdx, Sec);
				if (!Desc.bSupportsSecondary)
				{
					Sec = -1;
				}
				if (!LocalToShared.IsValidIndex(LocalIdx))
				{
					continue;
				}
				const int32 SharedIdx = LocalToShared[LocalIdx];
				if (SharedIdx < 0)
				{
					continue;
				}
				const uint64 Hash = Packer.GetPickIdx(SharedCollection, static_cast<int16>(SharedIdx), Sec);
				EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash));
			}
		}
	}
#endif // WITH_EDITOR
}

#pragma endregion

#pragma region UPCGExPCGDataAssetCollection

UPCGExPCGDataAssetCollection::UPCGExPCGDataAssetCollection()
{
	// Always-present machinery state. Default subobject: delta-serializes against
	// the CDO's, inherits RF_Transactional from the asset (RF_PropagateToSubObjects), and
	// guarantees the nested "External Storage" details block is never None.
	MachineryState = CreateDefaultSubobject<UPCGExPCGDataTypeState>(TEXT("MachineryState"));
}

void UPCGExPCGDataAssetCollection::PostLoad()
{
	Super::PostLoad();

	// Legacy machinery members migrate into MachineryState.
	// The deprecated slots still LOAD legacy data (UHT registers them under their unsuffixed
	// names with CPF_Deprecated: tagged properties match, saves always skip) -- move the
	// values over once and clear. Referenced subobjects keep their outer (this collection),
	// the state only holds the refs -- same shape as an Omni host.
	if (MachineryState)
	{
		// The state's own PostLoad adopts ITS legacy members; run it first so the slots are settled
		// before the pre-C1 members below are adopted through the same idempotent helper.
		MachineryState->ConditionalPostLoad();

		if (bUseExternalAssets_DEPRECATED)
		{
			MachineryState->bUseExternalAssets = true;
			bUseExternalAssets_DEPRECATED = false;
		}
		if (!ExportFolder_DEPRECATED.Path.IsEmpty())
		{
			MachineryState->ExportFolder = ExportFolder_DEPRECATED;
			ExportFolder_DEPRECATED.Path.Reset();
		}

		MachineryState->AdoptLegacySlot(PCGExLevelExport::Slots::Meshes, SharedMeshCollection_DEPRECATED, ExternalSharedMeshCollection_DEPRECATED.ToSoftObjectPath());
		SharedMeshCollection_DEPRECATED = nullptr;
		ExternalSharedMeshCollection_DEPRECATED.Reset();

		MachineryState->AdoptLegacySlot(PCGExLevelExport::Slots::Levels, SharedLevelCollection_DEPRECATED, ExternalSharedLevelCollection_DEPRECATED.ToSoftObjectPath());
		SharedLevelCollection_DEPRECATED = nullptr;
		ExternalSharedLevelCollection_DEPRECATED.Reset();
	}
}

bool UPCGExPCGDataAssetCollection::GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const
{
	if (!StructType || !StructType->IsChildOf(FPCGExPCGDataAssetCollectionGlobals::StaticStruct()))
	{
		return Super::GetTypeGlobalsInternal(StructType, OutGlobals);
	}

#if WITH_EDITORONLY_DATA
	FPCGExPCGDataAssetCollectionGlobals& Out = static_cast<FPCGExPCGDataAssetCollectionGlobals&>(OutGlobals);
	Out.LevelExporter = LevelExporter;
#endif
	return true;
}

bool UPCGExPCGDataAssetCollection::HostSupportsDataAssetMachinery(const UPCGExAssetCollection* Host)
{
	// Native lineage runs its own machinery; heterogeneous hosts answer through their
	// registered type-state capability.
	return Host && Host->SupportsTypeMachinery(PCGExAssetCollection::TypeIds::PCGDataAsset);
}

FString UPCGExPCGDataAssetCollection::MakeExternalAssetPrefixFor(const UPCGExAssetCollection* Host)
{
	return FString::Printf(TEXT("G_%08X"), Host ? Host->GetCollectionGUID() : 0u);
}

void UPCGExPCGDataAssetCollection::CompactSharedFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITOR
	if (!State.IsValid())
	{
		return;
	}

	TArray<PCGExLevelExport::FHandlerRegistration> Registrations;
	PCGExLevelExport::FHandlerRegistry::Get().GetRegistrations(Registrations);

	for (const PCGExLevelExport::FHandlerRegistration& Registration : Registrations)
	{
		const FPCGExExportSlotDesc& Desc = Registration.Desc;
		if (Desc.Scope != EPCGExExportSlotScope::Shared || !Registration.Policy)
		{
			continue;
		}

		// No storage is minted for a slot nothing contributes to and nothing held before.
		bool bHasContributions = false;
		for (const FPCGExPCGDataAssetCollectionEntry* E : State.Entries)
		{
			const FPCGExExportSlotCapture* Capture = PCGExExportSlots::Find(E->Captures, Desc.SlotId);
			if (Capture && !Capture->Entries.IsEmpty())
			{
				bHasContributions = true;
				break;
			}
		}
		if (!bHasContributions && !PCGExExportSlots::Find(*State.SharedSlots, Desc.SlotId))
		{
			continue;
		}

		FPCGExExportCollectionSlot& Slot = PCGExExportSlots::FindOrAdd(*State.SharedSlots, Desc.SlotId);
		PCGExSharedCompact::CompactSharedSlot(State.Host, State.Entries, Desc, *Registration.Policy, Slot, State.bExternalActive);
	}
#endif
}

void UPCGExPCGDataAssetCollection::RebuildCollectionMapsFor(FPCGExPCGDataAssetMachinery& State)
{
	if (!State.IsValid())
	{
		return;
	}

	for (FPCGExPCGDataAssetCollectionEntry* EntryPtr : State.Entries)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = *EntryPtr;
		if (!Entry.ExportedDataAsset)
		{
			continue;
		}

		PCGExCollections::FPickPacker FullPacker;
		for (const FPCGExExportCollectionSlot& Slot : *State.SharedSlots)
		{
			if (Slot.Collection)
			{
				FullPacker.RegisterCollection(Slot.Collection);
			}
		}
		for (const FPCGExExportCollectionSlot& Slot : Entry.EmbeddedSlots)
		{
			if (Slot.Collection)
			{
				FullPacker.RegisterCollection(Slot.Collection);
			}
		}

		Entry.ExportedDataAsset->Data.TaggedData.RemoveAll([](const FPCGTaggedData& TD)
		{
			return TD.Pin == PCGExCollections::Labels::CollectionMapPin;
		});

		UPCGParamData* MapData = NewObject<UPCGParamData>(Entry.ExportedDataAsset);
		FullPacker.PackToDataset(MapData);
		FPCGTaggedData& MapTaggedData = Entry.ExportedDataAsset->Data.TaggedData.Emplace_GetRef();
		MapTaggedData.Data = MapData;
		MapTaggedData.Pin = PCGExCollections::Labels::CollectionMapPin;
	}
}

void UPCGExPCGDataAssetCollection::ExternalizeSlotCollectionsFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITOR
	if (!State.IsValid() || !State.bExternalActive)
	{
		return;
	}

	// Naming uses the collection's GUID for cross-collection uniqueness in a shared export
	// folder, and is short enough to stay within filesystem path budgets. GUID is stable
	// across rebuilds -- filenames are reused (P4-friendly overwrites). The slot id is the
	// suffix, so the built-in slots keep their historical file names.
	const FString& FolderPath = State.ExportFolderPath;
	const FString& GuidPrefix = State.ExternalAssetPrefix;

	for (FPCGExExportCollectionSlot& Slot : *State.SharedSlots)
	{
		if (!Slot.Collection)
		{
			continue;
		}
		const FString AssetName = FString::Printf(TEXT("%s_%s"), *GuidPrefix, *Slot.SlotId.ToString());
		Slot.External = TSoftObjectPtr<UPCGExAssetCollection>(PCGExSharedCompact::ExternalizeUObject(Slot.Collection, FolderPath / AssetName, AssetName));
	}

	// Per-entry slots. Done before RebuildCollectionMaps so the soft paths the packer bakes into
	// the CollectionMap pin already point at the external assets.
	for (int32 EntryIdx = 0; EntryIdx < State.Entries.Num(); EntryIdx++)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = *State.Entries[EntryIdx];
		for (FPCGExExportCollectionSlot& Slot : Entry.EmbeddedSlots)
		{
			if (!Slot.Collection)
			{
				continue;
			}
			const FString AssetName = FString::Printf(TEXT("%s_E%03d_%s"), *GuidPrefix, EntryIdx, *Slot.SlotId.ToString());
			Slot.External = TSoftObjectPtr<UPCGExAssetCollection>(PCGExSharedCompact::ExternalizeUObject(Slot.Collection, FolderPath / AssetName, AssetName));
		}
	}
#endif
}

void UPCGExPCGDataAssetCollection::ExternalizeExportedDataAssetsFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITOR
	if (!State.IsValid() || !State.bExternalActive)
	{
		return;
	}

	const FString& FolderPath = State.ExportFolderPath;
	const FString& GuidPrefix = State.ExternalAssetPrefix;

	for (int32 EntryIdx = 0; EntryIdx < State.Entries.Num(); EntryIdx++)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = *State.Entries[EntryIdx];
		if (!Entry.ExportedDataAsset)
		{
			continue;
		}

		const FString AssetName = FString::Printf(TEXT("%s_E%03d_Data"), *GuidPrefix, EntryIdx);
		Entry.ExternalExportedDataAsset = PCGExSharedCompact::ExternalizeUObject(Entry.ExportedDataAsset, FolderPath / AssetName, AssetName);

		// Staging.Path is the runtime soft-load address for this entry. Repoint it at the
		// external location so LoadPCGData soft-loads from the external uasset, not the
		// (now-orphaned) inner-subobject path.
		Entry.Staging.Path = Entry.ExternalExportedDataAsset.ToSoftObjectPath();
	}
#endif
}

void UPCGExPCGDataAssetCollection::InternalizeSubobjectsFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITOR
	if (!State.IsValid())
	{
		return;
	}

	// Pull each externalized subobject back into the host's package and null the
	// soft refs. Used on External -> Embedded toggle. CompactSharedSlot's load-back path
	// also rehydrates shared collections lazily, but per-entry assets need explicit
	// internalization here because they have no equivalent build-time fallback.
	using namespace PCGExSharedCompact;

	for (FPCGExExportCollectionSlot& Slot : *State.SharedSlots)
	{
		Internalize(Slot.Collection, Slot.External, State.Host);
	}

	for (FPCGExPCGDataAssetCollectionEntry* EntryPtr : State.Entries)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = *EntryPtr;
		for (FPCGExExportCollectionSlot& Slot : Entry.EmbeddedSlots)
		{
			Internalize(Slot.Collection, Slot.External, State.Host);
		}
		Internalize(Entry.ExportedDataAsset, Entry.ExternalExportedDataAsset, State.Host);
		if (Entry.ExportedDataAsset)
		{
			Entry.Staging.Path = FSoftObjectPath(Entry.ExportedDataAsset);
		}
	}
#endif
}

void UPCGExPCGDataAssetCollection::CollectExternalPackagesFor(
	const UPCGExAssetCollection* Host,
	const TArray<FPCGExExportCollectionSlot>& InSharedSlots,
	const TArray<const FPCGExPCGDataAssetCollectionEntry*>& InEntries,
	TSet<UPackage*>& OutPackages)
{
#if WITH_EDITOR
	// Host and transient packages are excluded here so embedded-mode storage (outered to the
	// host) contributes nothing -- callers never need an external-active gate.
	UPackage* HostPackage = Host ? Host->GetOutermost() : nullptr;

	auto AddPackageFor = [&OutPackages, HostPackage](const UObject* Obj)
	{
		if (!Obj)
		{
			return;
		}
		UPackage* Pkg = Obj->GetOutermost();
		if (Pkg && Pkg != GetTransientPackage() && Pkg != HostPackage)
		{
			OutPackages.Add(Pkg);
		}
	};

	for (const FPCGExExportCollectionSlot& Slot : InSharedSlots)
	{
		AddPackageFor(Slot.Collection);
	}
	for (const FPCGExPCGDataAssetCollectionEntry* Entry : InEntries)
	{
		for (const FPCGExExportCollectionSlot& Slot : Entry->EmbeddedSlots)
		{
			AddPackageFor(Slot.Collection);
		}
		AddPackageFor(Entry->ExportedDataAsset);
	}
#endif
}

void UPCGExPCGDataAssetCollection::SaveExternalPackagesFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITOR
	if (!State.IsValid())
	{
		return;
	}

	TArray<const FPCGExPCGDataAssetCollectionEntry*> ConstEntries;
	ConstEntries.Append(State.Entries);

	TSet<UPackage*> Packages;
	CollectExternalPackagesFor(State.Host, *State.SharedSlots, ConstEntries, Packages);

	for (UPackage* Pkg : Packages)
	{
		const FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
	}
#endif
}

void UPCGExPCGDataAssetCollection::RebuildSharedCollectionsFor(FPCGExPCGDataAssetMachinery& State)
{
	CompactSharedFor(State);

	// Externalize shared + per-entry slot collections BEFORE the CollectionMap is baked
	// so the soft paths recorded in the map already point at their external packages.
	// Externalize* cores short-circuit internally when external mode isn't active.
	ExternalizeSlotCollectionsFor(State);

	RebuildCollectionMapsFor(State);

	// Externalize ExportedDataAsset AFTER the map is baked. The map lives inside
	// ExportedDataAsset as an inner UPCGParamData and moves with the rename; the
	// FSoftObjectPath values it carries are by-value and unaffected.
	ExternalizeExportedDataAssetsFor(State);
}

void UPCGExPCGDataAssetCollection::SaveExternalPackages()
{
#if WITH_EDITOR
	if (MachineryState)
	{
		FPCGExPCGDataAssetMachinery State = MachineryState->MakeMachinery(this);
		SaveExternalPackagesFor(State);
	}
#endif
}

void UPCGExPCGDataAssetCollection::RebuildSharedCollections()
{
	if (MachineryState)
	{
		FPCGExPCGDataAssetMachinery State = MachineryState->MakeMachinery(this);
		RebuildSharedCollectionsFor(State);
	}
}

void UPCGExPCGDataAssetCollection::ScrubSharedRefsForSave(TArray<FPCGExExportCollectionSlot>& Slots, FPCGExPCGDataSharedScrubKeep& OutKeep)
{
	OutKeep.Slots = &Slots;
	OutKeep.Kept.SetNum(Slots.Num());
	for (int32 i = 0; i < Slots.Num(); i++)
	{
		OutKeep.Kept[i] = Slots[i].Collection;
		Slots[i].Collection = nullptr;
	}
}

void UPCGExPCGDataAssetCollection::RestoreSharedRefsAfterSave(FPCGExPCGDataSharedScrubKeep& Keep)
{
	if (Keep.Slots)
	{
		const int32 N = FMath::Min(Keep.Slots->Num(), Keep.Kept.Num());
		for (int32 i = 0; i < N; i++)
		{
			(*Keep.Slots)[i].Collection = Keep.Kept[i];
		}
	}
	Keep = FPCGExPCGDataSharedScrubKeep();
}

void UPCGExPCGDataAssetCollection::ScrubEntryRefsForSave(const TArray<FPCGExPCGDataAssetCollectionEntry*>& InEntries, FPCGExPCGDataEntryScrubKeep& OutKeep)
{
	OutKeep.Reset();
	OutKeep.Entries.Reserve(InEntries.Num());
	OutKeep.Data.Reserve(InEntries.Num());
	OutKeep.Embedded.Reserve(InEntries.Num());

	for (FPCGExPCGDataAssetCollectionEntry* Entry : InEntries)
	{
		OutKeep.Entries.Add(Entry);
		OutKeep.Data.Add(Entry->ExportedDataAsset);
		Entry->ExportedDataAsset = nullptr;

		TArray<TObjectPtr<UPCGExAssetCollection>>& Kept = OutKeep.Embedded.AddDefaulted_GetRef();
		Kept.SetNum(Entry->EmbeddedSlots.Num());
		for (int32 i = 0; i < Entry->EmbeddedSlots.Num(); i++)
		{
			Kept[i] = Entry->EmbeddedSlots[i].Collection;
			Entry->EmbeddedSlots[i].Collection = nullptr;
		}
	}
}

void UPCGExPCGDataAssetCollection::RestoreEntryRefsAfterSave(FPCGExPCGDataEntryScrubKeep& Keep)
{
	for (int32 i = 0; i < Keep.Entries.Num(); i++)
	{
		FPCGExPCGDataAssetCollectionEntry* Entry = Keep.Entries[i];
		Entry->ExportedDataAsset = Keep.Data[i];
		const TArray<TObjectPtr<UPCGExAssetCollection>>& Kept = Keep.Embedded[i];
		const int32 N = FMath::Min(Kept.Num(), Entry->EmbeddedSlots.Num());
		for (int32 s = 0; s < N; s++)
		{
			Entry->EmbeddedSlots[s].Collection = Kept[s];
		}
	}
	Keep.Reset();
}

void UPCGExPCGDataAssetCollection::Serialize(FArchive& Ar)
{
	// Host-side entry-ref scrub around the save, dispatched to the machinery state -- the
	// exact code path an Omni host runs. Begin gates on external-active internally; the
	// state scrubs its OWN shared members in its own Serialize (it is a separate package
	// export, serialized by SavePackage outside this pair -- under the same save, so its
	// own gate holds).
	const bool bScrub = Ar.IsSaving() && !Ar.IsTransacting() && MachineryState;

	if (bScrub)
	{
		MachineryState->OnHostSerializeSave_Begin(this);
	}

	Super::Serialize(Ar);

	if (bScrub)
	{
		MachineryState->OnHostSerializeSave_End(this);
	}
}

void UPCGExPCGDataAssetCollection::PostDuplicate(bool bDuplicateForPIE)
{
	// Super first: the duplicate's CollectionGUID must be regenerated before the state
	// re-stamps anything keyed to it.
	Super::PostDuplicate(bDuplicateForPIE);

	if (MachineryState)
	{
		MachineryState->OnHostPostDuplicate(this, bDuplicateForPIE);
	}
}

void UPCGExPCGDataAssetCollection::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Cook-time safety net -- gated and explained in UPCGExPCGDataTypeState::OnHostPreSave
	// (including why SaveExternalPackages is deliberately NOT called during cook).
	if (MachineryState)
	{
		MachineryState->OnHostPreSave(this, ObjectSaveContext);
	}

	Super::PreSave(ObjectSaveContext);
}

#if WITH_EDITOR

void UPCGExPCGDataAssetCollection::EDITOR_OnPostStagingRebuild()
{
	EDITOR_RunTypeStatesPostStaging();
}

void UPCGExPCGDataAssetCollection::EDITOR_RunTypeStatesPostStaging()
{
	if (MachineryState)
	{
		MachineryState->EDITOR_OnHostPostStagingRebuild(this);
	}
}

void UPCGExPCGDataAssetCollection::EDITOR_OnHostRelocated()
{
	if (MachineryState)
	{
		MachineryState->EDITOR_OnHostRelocated(this);
	}
}

void UPCGExPCGDataAssetCollection::EDITOR_GetExternalPackages(TSet<UPackage*>& OutPackages) const
{
	if (MachineryState)
	{
		MachineryState->EDITOR_AppendExternalPackages(this, OutPackages);
	}
}

void UPCGExPCGDataAssetCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		// Try as UWorld (Level source)
		if (SelectedAsset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
		{
			TSoftObjectPtr<UWorld> WorldAsset(SelectedAsset.GetSoftObjectPath());

			bool bAlreadyExists = false;
			for (const FPCGExPCGDataAssetCollectionEntry& ExistingEntry : Entries)
			{
				if (ExistingEntry.Source == EPCGExDataAssetEntrySource::Level && ExistingEntry.Level == WorldAsset)
				{
					bAlreadyExists = true;
					break;
				}
			}

			if (bAlreadyExists)
			{
				continue;
			}

			FPCGExPCGDataAssetCollectionEntry Entry;
			Entry.Source = EPCGExDataAssetEntrySource::Level;
			Entry.Level = WorldAsset;
			Entries.Add(Entry);
			continue;
		}

		// Try as UPCGDataAsset (DataAsset source)
		TSoftObjectPtr<UPCGDataAsset> Asset = TSoftObjectPtr<UPCGDataAsset>(SelectedAsset.ToSoftObjectPath());
		if (!Asset.LoadSynchronous())
		{
			continue;
		}

		bool bAlreadyExists = false;
		for (const FPCGExPCGDataAssetCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.Source == EPCGExDataAssetEntrySource::DataAsset && ExistingEntry.DataAsset == Asset)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}

		FPCGExPCGDataAssetCollectionEntry Entry;
		Entry.Source = EPCGExDataAssetEntrySource::DataAsset;
		Entry.DataAsset = Asset;
		Entries.Add(Entry);
	}
}

void UPCGExPCGDataAssetCollection::AppendCookDependencyAssetPathsFor(
	const TArray<FPCGExExportCollectionSlot>& InSharedSlots,
	const TArray<const FPCGExPCGDataAssetCollectionEntry*>& InEntries,
	TSet<FSoftObjectPath>& OutPaths)
{
	// Embedded slot collections live in the host's package so the package itself is already in the
	// cook -- but their *entries* hold soft refs to the actual meshes / levels / sketches which cook
	// traversal won't reach on its own. Externalized ones sit in their own packages -- surface them so
	// the ModifyCook scan force-cooks those packages too; once cooked, the registry scan re-enters them
	// (they're UPCGExAssetCollection subclasses and implement the interface), so their leaf soft refs
	// follow automatically.
	auto AppendSlot = [&OutPaths](const FPCGExExportCollectionSlot& Slot)
	{
		if (Slot.Collection)
		{
			Slot.Collection->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
		}
		if (!Slot.External.IsNull())
		{
			OutPaths.Add(Slot.External.ToSoftObjectPath());
		}
	};

	for (const FPCGExExportCollectionSlot& Slot : InSharedSlots)
	{
		AppendSlot(Slot);
	}

	// Per-entry slot collections hang off the entry as hard subobjects rather than entries[], so the
	// base walk skips them.
	for (const FPCGExPCGDataAssetCollectionEntry* Entry : InEntries)
	{
		for (const FPCGExExportCollectionSlot& Slot : Entry->EmbeddedSlots)
		{
			AppendSlot(Slot);
		}
	}
}

void UPCGExPCGDataAssetCollection::GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	// Base = each entry's Staging.Path. For Level-sourced entries that path is the embedded
	// (or repointed external) ExportedDataAsset; for DataAsset-sourced entries it's the
	// user-referenced UPCGDataAsset. The machinery refs (shared collections, externals,
	// per-entry actor collections) come from the state dispatch.
	Super::GetCookDependencyAssetPaths(OutPaths);

	if (MachineryState)
	{
		MachineryState->AppendCookDependencyAssetPaths(this, OutPaths);
	}
}
#endif

#pragma endregion

#pragma region UPCGExPCGDataTypeState

FPCGExPCGDataAssetMachinery UPCGExPCGDataTypeState::MakeMachinery(UPCGExAssetCollection* Host)
{
	FPCGExPCGDataAssetMachinery State;
	State.Host = Host;

	if (Host)
	{
		Host->ForEachEntry([&State](FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (Entry->IsType(PCGExAssetCollection::TypeIds::PCGDataAsset))
			{
				State.Entries.Add(static_cast<FPCGExPCGDataAssetCollectionEntry*>(Entry));
			}
		});
	}

	State.SharedSlots = &SharedSlots;

	State.bExternalActive = IsExternalActive();
	State.ExportFolderPath = ExportFolder.Path;
	// Keyed to the HOST's GUID so filenames are host-unique and rebuild-stable.
	State.ExternalAssetPrefix = UPCGExPCGDataAssetCollection::MakeExternalAssetPrefixFor(Host);

	return State;
}

void UPCGExPCGDataTypeState::OnHostPreSave(UPCGExAssetCollection* Host, FObjectPreSaveContext SaveContext)
{
	// Cook-time safety net for users who edited a source level and cooked without a manual
	// rebuild (or recovered from a mid-edit editor crash). Idempotent.
	//
	// Only re-bake the IN-MEMORY state here.
	// We deliberately do NOT call SaveExternalPackages during cook:
	//  - SavePackage(Pkg, nullptr, ...) is an editor (uncooked) save that writes the SOURCE
	//    .uasset under /Content/. A cook must be read-only w.r.t. source content; writing it
	//    dirties the workspace and fails / forces a writable-flip on read-only (Perforce) files.
	//  - It mutates packages the cooker may have already cooked (no effect) or not yet cooked
	//    (changing the source mid-cook), making the output depend on cook scheduling.
	//  - In concurrent / cook-by-the-book saving, GIsSavingPackage is held for the whole batch;
	//    a nested non-concurrent SavePackage clears it on scope-exit, breaking that invariant.
	// In most cases (except potential multi-threaded cooks), external objects are cooked from
	// the loaded in-memory ones, which should be enough. NOTE: levels are not re-harvested
	// here, so this net is not comprehensive -- kept as a best-effort re-bake.
	if (SaveContext.IsCooking())
	{
		FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
		UPCGExPCGDataAssetCollection::RebuildSharedCollectionsFor(State);
	}
}

void UPCGExPCGDataTypeState::OnHostPostDuplicate(UPCGExAssetCollection* Host, bool bDuplicateForPIE)
{
	// Mirrors UPCGExPCGDataAssetCollection::PostDuplicate: the duplicate's collections carry
	// fresh GUIDs while per-entry ExportedDataAssets still hold hashes keyed to the originals.
	if (!bDuplicateForPIE)
	{
		FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
		UPCGExPCGDataAssetCollection::RebuildSharedCollectionsFor(State);
	}
}

void UPCGExPCGDataTypeState::OnHostSerializeSave_Begin(UPCGExAssetCollection* Host)
{
	// Entry-level instanced refs live in HOST data (payload rows) -- null them around the
	// host's save so no hard references to session buffers / externalized assets are baked.
	// Same core as the typed collection's Serialize scrub; state-OWNED members are scrubbed
	// in this object's own Serialize instead (separate package export).
	if (!IsExternalActive() || !Host)
	{
		return;
	}

	FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
	UPCGExPCGDataAssetCollection::ScrubEntryRefsForSave(State.Entries, ScrubKeep);
}

void UPCGExPCGDataTypeState::OnHostSerializeSave_End(UPCGExAssetCollection* Host)
{
	// Restores exactly what Begin scrubbed; no-op when Begin's gate skipped.
	UPCGExPCGDataAssetCollection::RestoreEntryRefsAfterSave(ScrubKeep);
}

void UPCGExPCGDataTypeState::Serialize(FArchive& Ar)
{
	// Own-member mirror of the typed collection's external-mode scrub (same core): shared
	// collections are session working buffers whose targets live in external packages -- keep
	// their instanced refs out of the saved data. Transacting round-trips in-memory state.
	if (IsExternalActive() && Ar.IsSaving() && !Ar.IsTransacting())
	{
		FPCGExPCGDataSharedScrubKeep SharedKeep;
		UPCGExPCGDataAssetCollection::ScrubSharedRefsForSave(SharedSlots, SharedKeep);

		Super::Serialize(Ar);

		UPCGExPCGDataAssetCollection::RestoreSharedRefsAfterSave(SharedKeep);
	}
	else
	{
		Super::Serialize(Ar);
	}
}

void UPCGExPCGDataTypeState::PostLoad()
{
	Super::PostLoad();

	AdoptLegacySlot(PCGExLevelExport::Slots::Meshes, SharedMeshCollection_DEPRECATED, ExternalSharedMeshCollection_DEPRECATED.ToSoftObjectPath());
	SharedMeshCollection_DEPRECATED = nullptr;
	ExternalSharedMeshCollection_DEPRECATED.Reset();

	AdoptLegacySlot(PCGExLevelExport::Slots::Levels, SharedLevelCollection_DEPRECATED, ExternalSharedLevelCollection_DEPRECATED.ToSoftObjectPath());
	SharedLevelCollection_DEPRECATED = nullptr;
	ExternalSharedLevelCollection_DEPRECATED.Reset();
}

void UPCGExPCGDataTypeState::AdoptLegacySlot(const FName SlotId, UPCGExAssetCollection* Collection, const FSoftObjectPath& External)
{
	if (!Collection && External.IsNull())
	{
		return;
	}

	FPCGExExportCollectionSlot& Slot = PCGExExportSlots::FindOrAdd(SharedSlots, SlotId);
	if (!Slot.Collection && Collection)
	{
		Slot.Collection = Collection;
	}
	if (Slot.External.IsNull() && !External.IsNull())
	{
		Slot.External = TSoftObjectPtr<UPCGExAssetCollection>(External);
	}
}

#if WITH_EDITOR

void UPCGExPCGDataTypeState::EDITOR_OnHostPostStagingRebuild(UPCGExAssetCollection* Host)
{
	FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
	UPCGExPCGDataAssetCollection::RebuildSharedCollectionsFor(State);
}

void UPCGExPCGDataTypeState::EDITOR_OnHostRelocated(UPCGExAssetCollection* Host)
{
	FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
	if (!State.IsValid())
	{
		return;
	}

	for (FPCGExPCGDataAssetCollectionEntry* Entry : State.Entries)
	{
		if (Entry && Entry->ExportedDataAsset && Entry->ExportedDataAsset->IsIn(Host))
		{
			Entry->Staging.Path = FSoftObjectPath(Entry->ExportedDataAsset);
		}
	}

	// Re-packs from the live shared / per-entry collections, wherever they now live.
	UPCGExPCGDataAssetCollection::RebuildCollectionMapsFor(State);
}

void UPCGExPCGDataTypeState::AppendCookDependencyAssetPaths(const UPCGExAssetCollection* Host, TSet<FSoftObjectPath>& OutPaths) const
{
	// Same core as UPCGExPCGDataAssetCollection::GetCookDependencyAssetPaths, driven by THIS
	// state's storage and the host's PCGData-typed leaf payloads.
	TArray<const FPCGExPCGDataAssetCollectionEntry*> EntryPtrs;
	if (Host)
	{
		Host->ForEachEntry([&EntryPtrs](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (Entry->IsType(PCGExAssetCollection::TypeIds::PCGDataAsset))
			{
				EntryPtrs.Add(static_cast<const FPCGExPCGDataAssetCollectionEntry*>(Entry));
			}
		});
	}

	UPCGExPCGDataAssetCollection::AppendCookDependencyAssetPathsFor(SharedSlots, EntryPtrs, OutPaths);
}

void UPCGExPCGDataTypeState::EDITOR_AppendExternalPackages(const UPCGExAssetCollection* Host, TSet<UPackage*>& OutPackages) const
{
	// Same view as AppendCookDependencyAssetPaths: THIS state's storage + the host's
	// PCGData-typed leaf payloads.
	TArray<const FPCGExPCGDataAssetCollectionEntry*> EntryPtrs;
	if (Host)
	{
		Host->ForEachEntry([&EntryPtrs](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (Entry->IsType(PCGExAssetCollection::TypeIds::PCGDataAsset))
			{
				EntryPtrs.Add(static_cast<const FPCGExPCGDataAssetCollectionEntry*>(Entry));
			}
		});
	}

	UPCGExPCGDataAssetCollection::CollectExternalPackagesFor(Host, SharedSlots, EntryPtrs, OutPackages);
}

void UPCGExPCGDataTypeState::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Ports UPCGExPCGDataAssetCollection::PostEditChangeProperty's external-storage reactions.
	// The engine's property-node up-walk delivers the edit HERE first, then to the outer host,
	// whose base PostEditChangeProperty runs the staging rebuild -> post-rebuild state dispatch
	// (RebuildSharedCollectionsFor with the new settings). So the rebuild half of the typed
	// reactions comes for free; what must happen here is the half the rebuild cannot do:
	// External -> Embedded internalization (pull externalized assets back into the host package
	// and clear the soft refs -- otherwise they stay externalized forever, cook deps keep
	// force-cooking orphaned packages, and a cold-session rebuild would mint a fresh shared
	// collection GUID, losing user edits and invalidating baked hashes).
	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const bool bToggledExternal = (PropName == GET_MEMBER_NAME_CHECKED(UPCGExPCGDataTypeState, bUseExternalAssets));

	if (bToggledExternal && !bUseExternalAssets)
	{
		if (UPCGExAssetCollection* Host = GetTypedOuter<UPCGExAssetCollection>())
		{
			FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
			UPCGExPCGDataAssetCollection::InternalizeSubobjectsFor(State);
		}
	}
}

void UPCGExPCGDataTypeState::OnAddedToHost(UPCGExAssetCollection* Host, const UPCGExAssetCollection* SeedSource)
{
	// Adopt the seed source's external-storage SETTINGS on creation. Only fires on FRESH states -- an existing state is the user's and always
	// wins (behavior-wins, like globals-block merging). Only settings transfer: shared
	// collections are session buffers, and External* soft refs address the SOURCE's own
	// external packages -- adopting them would alias another asset's storage. Sharing the
	// source's ExportFolder is safe: external names are host-GUID-prefixed.
	if (!SeedSource)
	{
		return;
	}

	const UPCGExPCGDataTypeState* SourceState = SeedSource->FindTypeState<UPCGExPCGDataTypeState>();
	if (!SourceState || !SourceState->bUseExternalAssets)
	{
		return;
	}

	bUseExternalAssets = SourceState->bUseExternalAssets;
	ExportFolder = SourceState->ExportFolder;

	UE_LOG(LogPCGEx, Log,
	       TEXT("'%s': ensured PCG Data Asset machinery adopted external-storage settings (folder '%s') from source '%s'."),
	       Host ? *Host->GetName() : TEXT("<null>"), *ExportFolder.Path, *SeedSource->GetName());
}

void UPCGExPCGDataTypeState::OnSeedSourceIgnored(UPCGExAssetCollection* Host, const UPCGExAssetCollection* SeedSource)
{
	// First-creator-wins must not be silent when it drops real configuration: if the ignored
	// source ran external storage and this state's settings differ, the merged entries will
	// compact/externalize under THIS state's rules -- say so, so the author can re-apply the
	// source's settings deliberately.
	const UPCGExPCGDataTypeState* SourceState = SeedSource ? SeedSource->FindTypeState<UPCGExPCGDataTypeState>() : nullptr;
	if (!SourceState || !SourceState->bUseExternalAssets)
	{
		return;
	}

	if (bUseExternalAssets != SourceState->bUseExternalAssets || ExportFolder.Path != SourceState->ExportFolder.Path)
	{
		UE_LOG(LogPCGEx, Warning,
		       TEXT("'%s': merged source '%s' used external storage (folder '%s'), but this collection's existing PCG Data Asset machinery settings win (%s). Re-apply the source's settings manually if that was intended."),
		       Host ? *Host->GetName() : TEXT("<null>"), *SeedSource->GetName(), *SourceState->ExportFolder.Path,
		       bUseExternalAssets ? *FString::Printf(TEXT("external, folder '%s'"), *ExportFolder.Path) : TEXT("embedded"));
	}
}

void UPCGExPCGDataTypeState::OnRemovedFromHost(UPCGExAssetCollection* Host)
{
	// Teardown surface: external packages this machinery produced stay on disk (deleting
	// user-visible assets here would be data loss) -- but with the state gone nothing
	// references them anymore. Say so instead of orphaning silently.
	TArray<FString> Externals;
	for (const FPCGExExportCollectionSlot& Slot : SharedSlots)
	{
		if (!Slot.External.IsNull())
		{
			Externals.Add(Slot.External.ToSoftObjectPath().ToString());
		}
	}
	if (!Externals.IsEmpty())
	{
		UE_LOG(LogPCGEx, Warning,
		       TEXT("'%s': the removed PCG Data Asset machinery state still pointed at externalized packages (%s). They remain on disk, now orphaned -- delete them manually, or undo, re-add PCGData entries and disable external storage first to internalize them."),
		       Host ? *Host->GetName() : TEXT("<null>"),
		       *FString::Join(Externals, TEXT(", ")));
	}
}

#endif

#pragma endregion
