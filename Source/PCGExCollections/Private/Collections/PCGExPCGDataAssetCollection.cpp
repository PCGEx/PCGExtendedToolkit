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
#include "PCGExSocketProvider.h"
#include "PCGParamData.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Data/PCGPointArrayData.h"
#include "Data/PCGSpatialData.h"
#include "Engine/Level.h"
#include "Helpers/PCGExCollectionSortKeys.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExLevelDataExporter.h"
#include "PCGExSchemaMerging.h"


// Static-init type registration: TypeId=PCGDataAsset, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(PCGDataAsset, UPCGExPCGDataAssetCollection, FPCGExPCGDataAssetCollectionEntry, "PCG Data Asset Collection", Base)

#pragma region FPCGExPCGDataAssetCollectionEntry

bool FPCGExPCGDataAssetCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (Source == EPCGExDataAssetEntrySource::Level)
		{
			if (!Level.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
			{
				return false;
			}
		}
		else
		{
			if (!DataAsset.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
			{
				return false;
			}
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

// Loads the PCG data asset (or exports level data) and computes combined bounds.
//
// Level-sourced entries route through the 3-arg exporter API so EditorMeshContributions /
// EditorLocalPicks + EditorLevelContributions / EditorLevelLocalPicks are captured directly
// into the entry's UPROPERTY storage. Tag_EntryIdx hashes (Meshes + Levels pins) and the
// CollectionMap pin are NOT written here -- those are produced by the parent collection's
// CompactSharedMesh / CompactSharedLevel / RebuildCollectionMaps, which see every entry's
// contributions and resolve final shared indices.
void FPCGExPCGDataAssetCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	if (Source == EPCGExDataAssetEntrySource::Level)
	{
		// Level source: load world, export to embedded data asset
		TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(Level.ToSoftObjectPath());
		UWorld* LoadedWorld = Level.Get();

		if (!LoadedWorld)
		{
			Staging.Bounds = FBox(ForceInit);
			Staging.Path = FSoftObjectPath();
#if WITH_EDITORONLY_DATA
			EditorMeshContributions.Reset();
			EditorMeshInheritedDefaults.Reset();
			EditorLocalPicks.Reset();
			EditorLevelContributions.Reset();
			EditorLevelLocalPicks.Reset();
#endif
			PCGExHelpers::SafeReleaseHandle(Handle);
			FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
			return;
		}

		// Always recreate ExportedDataAsset fresh. Reusing + resetting TaggedData leaves orphaned
		// UPCGBasePointData subobjects in the outer chain that still serialize into the .uasset,
		// which causes save-time pointer traversal crashes after repeated rebuilds.
		if (ExportedDataAsset)
		{
			ExportedDataAsset->Rename(nullptr, GetTransientPackage(),
			                          REN_DontCreateRedirectors | REN_NonTransactional);
		}
		ExportedDataAsset = NewObject<UPCGDataAsset>(const_cast<UPCGExAssetCollection*>(OwningCollection));

		// Use collection's instanced exporter if available, otherwise create a transient default.
		// The instanced exporter is editor-only data; cooked builds always take the fallback path.
		UPCGExLevelDataExporter* Exporter = nullptr;
#if WITH_EDITORONLY_DATA
		if (const UPCGExPCGDataAssetCollection* TypedCollection = Cast<UPCGExPCGDataAssetCollection>(OwningCollection))
		{
			Exporter = TypedCollection->LevelExporter;
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

		// Wire the export context to write directly into the entry's UPROPERTY storage --
		// no copy at the API boundary. Only available in editor builds; shipping builds run
		// the exporter without capturing contributions (the shared collections are already
		// baked into the per-entry ExportedDataAsset hashes at cook time).
		FPCGExLevelExportContext ExportContext;
#if WITH_EDITORONLY_DATA
		// Reset editor-only capture buffers so a failed export doesn't leave stale data
		// from a prior rebuild contributing to CompactSharedMesh / CompactSharedLevel.
		EditorMeshContributions.Reset();
		EditorMeshInheritedDefaults.Reset();
		EditorLocalPicks.Reset();
		EditorLevelContributions.Reset();
		EditorLevelLocalPicks.Reset();
		ExportContext.MeshContributions = &EditorMeshContributions;
		ExportContext.MeshInheritedDefaults = &EditorMeshInheritedDefaults;
		ExportContext.MeshLocalPicks = &EditorLocalPicks;
		ExportContext.LevelContributions = &EditorLevelContributions;
		ExportContext.LevelLocalPicks = &EditorLevelLocalPicks;
#endif
		ExportContext.ActorCollectionOut = &EmbeddedActorCollection;

		EmbeddedActorCollection = nullptr;
		const bool bSuccess = Exporter->ExportLevelData(LoadedWorld, ExportedDataAsset, ExportContext);

		if (bSuccess)
		{
			Staging.Path = FSoftObjectPath(ExportedDataAsset);
			Staging.Bounds = PCGExPCGDataAssetCollectionInternal::ComputeBoundsFromAsset(ExportedDataAsset);

			// Scan for socket actors after export so the world is in the same initialized
			// state the exporter used -- transforms are reliable at this point.
			if (LoadedWorld->PersistentLevel)
			{
				for (AActor* Actor : LoadedWorld->PersistentLevel->Actors)
				{
					if (IPCGExSocketProvider* Provider = Cast<IPCGExSocketProvider>(Actor))
					{
						FPCGExSocket& NewSocket = Staging.Sockets.Emplace_GetRef(
							Provider->GetSocketName_Implementation(),
							Provider->GetSocketTransform_Implementation(),
							Provider->GetSocketTag_Implementation());
						NewSocket.bManaged = true;
					}
				}
			}
		}
		else
		{
			Staging.Path = FSoftObjectPath();
			Staging.Bounds = FBox(ForceInit);
#if WITH_EDITORONLY_DATA
			EditorMeshContributions.Reset();
			EditorMeshInheritedDefaults.Reset();
			EditorLocalPicks.Reset();
			EditorLevelContributions.Reset();
			EditorLevelLocalPicks.Reset();
#endif
		}

		PCGExHelpers::SafeReleaseHandle(Handle);
	}
	else
	{
		// DataAsset source: existing behavior. No mesh/level contributions captured.
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

#if WITH_EDITORONLY_DATA
		// DataAsset-sourced entries don't contribute to the shared collections -- clear any
		// stale contributions left behind by a prior Source==Level rebuild.
		EditorMeshContributions.Reset();
		EditorMeshInheritedDefaults.Reset();
		EditorLocalPicks.Reset();
		EditorLevelContributions.Reset();
		EditorLevelLocalPicks.Reset();
#endif

		PCGExHelpers::SafeReleaseHandle(Handle);
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
}

void FPCGExPCGDataAssetCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);

	if (Source == EPCGExDataAssetEntrySource::Level)
	{
		Level = TSoftObjectPtr<UWorld>(InPath);
	}
	else
	{
		DataAsset = TSoftObjectPtr<UPCGDataAsset>(InPath);
	}
}

#if WITH_EDITOR
void FPCGExPCGDataAssetCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	// Clean up embedded data when not in Level mode
	if (Source != EPCGExDataAssetEntrySource::Level)
	{
		ExportedDataAsset = nullptr;
		EmbeddedActorCollection = nullptr;
		EditorMeshContributions.Reset();
		EditorMeshInheritedDefaults.Reset();
		EditorLocalPicks.Reset();
		EditorLevelContributions.Reset();
		EditorLevelLocalPicks.Reset();
	}
}

void FPCGExPCGDataAssetCollectionEntry::EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	if (bIsSubCollection)
	{
		return;
	}

	// Source refs trigger rebuild -- not Staging.Path, which for Source==Level points at
	// an embedded ExportedDataAsset inside the collection's own package.
	if (Source == EPCGExDataAssetEntrySource::Level)
	{
		const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
		if (LevelPath.IsValid())
		{
			OutPaths.Emplace(LevelPath);
		}
	}
	else
	{
		const FSoftObjectPath AssetPath = DataAsset.ToSoftObjectPath();
		if (AssetPath.IsValid())
		{
			OutPaths.Emplace(AssetPath);
		}
	}
}

FSoftObjectPath FPCGExPCGDataAssetCollectionEntry::EDITOR_GetThumbnailAssetPath() const
{
	if (bIsSubCollection)
	{
		return FPCGExAssetCollectionEntry::EDITOR_GetThumbnailAssetPath();
	}

	// Level-sourced entries stage an embedded ExportedDataAsset inside the collection
	// package; show the user-facing source (the UWorld) instead.
	if (Source == EPCGExDataAssetEntrySource::Level)
	{
		return Level.ToSoftObjectPath();
	}

	return DataAsset.ToSoftObjectPath();
}
#endif

#pragma endregion

#pragma region SharedCompact internals

namespace PCGExSharedCompact
{
	static UPCGBasePointData* FindPointDataByPin(UPCGDataAsset* Asset, FName PinName)
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

	/**
	 * Symmetric to ExternalizeUObject. Loads the asset behind External (when valid) and
	 * renames it into NewOuter's package, populating Instanced. External is always reset.
	 * Guarded: if Instanced is already set we skip the load -- never overwrite an in-memory
	 * working buffer with a (possibly stale) on-disk copy.
	 */
	template <typename T>
	static void Internalize(TObjectPtr<T>& Instanced, TSoftObjectPtr<T>& External, UObject* NewOuter)
	{
#if WITH_EDITOR
		if (!Instanced && !External.IsNull())
		{
			if (T* Loaded = External.LoadSynchronous())
			{
				Loaded->Rename(nullptr, NewOuter, REN_DontCreateRedirectors | REN_NonTransactional);
				Loaded->ClearFlags(RF_Public | RF_Standalone);
				Instanced = Loaded;
			}
		}
		External.Reset();
#endif
	}

	/**
	 * Rename Source into the package at DesiredPackagePath as DesiredAssetName, evicting any
	 * existing UObject sitting at that path to the transient package first. On first emit,
	 * notifies the asset registry. Idempotent: skips work if Source is already in place.
	 *
	 * Returns the resulting soft path (empty if Source was null).
	 */
	static FSoftObjectPath ExternalizeUObject(UObject* Source, const FString& DesiredPackagePath, const FString& DesiredAssetName)
	{
#if WITH_EDITOR
		if (!Source)
		{
			return FSoftObjectPath();
		}

		UPackage* CurrentPackage = Source->GetOutermost();
		if (CurrentPackage && CurrentPackage->GetName() == DesiredPackagePath && Source->GetName() == DesiredAssetName)
		{
			return FSoftObjectPath(Source);
		}

		UPackage* TargetPackage = CreatePackage(*DesiredPackagePath);
		check(TargetPackage);

		// SavePackage refuses to overwrite a package that is only partially loaded -- doing so
		// would clobber the unloaded on-disk exports (see SavePackage2.cpp IsFullyLoaded guard).
		// When the destination already exists on disk but was never loaded this session,
		// CreatePackage hands back an unloaded stub (IsFullyLoaded() == false). This is the
		// common case during cook: per-entry ExportedDataAssets are reached only via soft refs
		// (Staging.Path), so a project-wide rebuild that force-loads collections never pulls the
		// data-asset packages in. Fully load the target first -- it both materializes the
		// existing occupant for the eviction below and makes the resulting package saveable.
		// Newly-created packages with no file on disk already report IsFullyLoaded() == true, so
		// this is a no-op for them. (Shared collections don't hit this because CompactShared
		// LoadSynchronous()'s their external package back before modifying it.)
		if (!TargetPackage->IsFullyLoaded())
		{
			TargetPackage->FullyLoad();
		}

		// If something already owns the target name in the target package, evict it. Renaming
		// onto a name conflict would otherwise assert; we expect this on subsequent rebuilds
		// when an old externalized asset is still loaded in the editor.
		const FName TargetName(*DesiredAssetName);
		if (UObject* Existing = StaticFindObjectFast(UObject::StaticClass(), TargetPackage, TargetName))
		{
			if (Existing != Source)
			{
				Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}

		Source->Rename(*DesiredAssetName, TargetPackage, REN_DontCreateRedirectors | REN_NonTransactional);
		Source->SetFlags(RF_Public | RF_Standalone);
		TargetPackage->SetFlags(RF_Public | RF_Standalone);
		TargetPackage->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(Source);

		return FSoftObjectPath(Source);
#else
		return Source ? FSoftObjectPath(Source) : FSoftObjectPath();
#endif
	}

	// --- Mesh identity ---
	// Identity hash deliberately omits Weight/Tags/Category/PropertyOverrides -- Weight
	// accumulates from contributors, the rest are user-owned on the shared entry.
	// Descriptor fields are not hashed (large structs); ContentEquals resolves collisions.
	static uint32 MeshContentHash(const FPCGExMeshCollectionEntry& E)
	{
		uint32 H = GetTypeHash(E.StaticMesh.ToSoftObjectPath());
		H = HashCombine(H, GetTypeHash(static_cast<uint8>(E.MaterialVariants)));
		H = HashCombine(H, GetTypeHash(E.SlotIndex));
		H = HashCombine(H, GetTypeHash(static_cast<uint8>(E.DescriptorSource)));

		H = HashCombine(H, GetTypeHash(E.MaterialOverrideVariants.Num()));
		for (const FPCGExMaterialOverrideSingleEntry& S : E.MaterialOverrideVariants)
		{
			H = HashCombine(H, GetTypeHash(S.Weight));
			H = HashCombine(H, GetTypeHash(S.Material.ToSoftObjectPath()));
		}

		H = HashCombine(H, GetTypeHash(E.MaterialOverrideVariantsList.Num()));
		for (const FPCGExMaterialOverrideCollection& V : E.MaterialOverrideVariantsList)
		{
			H = HashCombine(H, GetTypeHash(V.Weight));
			H = HashCombine(H, GetTypeHash(V.Overrides.Num()));
			for (const FPCGExMaterialOverrideEntry& O : V.Overrides)
			{
				H = HashCombine(H, GetTypeHash(O.SlotIndex));
				H = HashCombine(H, GetTypeHash(O.Material.ToSoftObjectPath()));
			}
		}

		// Property-component identity. Zero (no source component) hashes as zero, so entries
		// without a property collection still aggregate alongside each other -- only entries
		// that actually authored distinct property data split into separate buckets.
		H = HashCombine(H, E.PropertyComponentHash);

		return H;
	}

	static bool MaterialOverrideEquals(const FPCGExMaterialOverrideSingleEntry& A, const FPCGExMaterialOverrideSingleEntry& B)
	{
		return A.Weight == B.Weight && A.Material.ToSoftObjectPath() == B.Material.ToSoftObjectPath();
	}

	static bool MaterialOverrideEquals(const FPCGExMaterialOverrideEntry& A, const FPCGExMaterialOverrideEntry& B)
	{
		return A.SlotIndex == B.SlotIndex && A.Material.ToSoftObjectPath() == B.Material.ToSoftObjectPath();
	}

	static bool MaterialOverrideEquals(const FPCGExMaterialOverrideCollection& A, const FPCGExMaterialOverrideCollection& B)
	{
		if (A.Weight != B.Weight)
		{
			return false;
		}
		if (A.Overrides.Num() != B.Overrides.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Overrides.Num(); i++)
		{
			if (!MaterialOverrideEquals(A.Overrides[i], B.Overrides[i]))
			{
				return false;
			}
		}
		return true;
	}

	static bool MeshContentEquals(const FPCGExMeshCollectionEntry& A, const FPCGExMeshCollectionEntry& B)
	{
		if (A.StaticMesh.ToSoftObjectPath() != B.StaticMesh.ToSoftObjectPath())
		{
			return false;
		}
		if (A.MaterialVariants != B.MaterialVariants)
		{
			return false;
		}
		if (A.SlotIndex != B.SlotIndex)
		{
			return false;
		}
		if (A.DescriptorSource != B.DescriptorSource)
		{
			return false;
		}

		if (A.MaterialOverrideVariants.Num() != B.MaterialOverrideVariants.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.MaterialOverrideVariants.Num(); i++)
		{
			if (!MaterialOverrideEquals(A.MaterialOverrideVariants[i], B.MaterialOverrideVariants[i]))
			{
				return false;
			}
		}

		if (A.MaterialOverrideVariantsList.Num() != B.MaterialOverrideVariantsList.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.MaterialOverrideVariantsList.Num(); i++)
		{
			if (!MaterialOverrideEquals(A.MaterialOverrideVariantsList[i], B.MaterialOverrideVariantsList[i]))
			{
				return false;
			}
		}

		if (!FSoftISMComponentDescriptor::StaticStruct()->CompareScriptStruct(&A.ISMDescriptor, &B.ISMDescriptor, 0))
		{
			return false;
		}
		if (!FPCGExStaticMeshComponentDescriptor::StaticStruct()->CompareScriptStruct(&A.SMDescriptor, &B.SMDescriptor, 0))
		{
			return false;
		}

		// Property-component identity must match for two entries to dedup. Crucial when
		// otherwise-identical mesh actors carry distinct property values: we want them in
		// separate shared entries so their per-instance PropertyOverrides survive.
		if (A.PropertyComponentHash != B.PropertyComponentHash)
		{
			return false;
		}

		return true;
	}

#if WITH_EDITOR
	// Policies + CompactShared reference Editor* fields on FPCGExPCGDataAssetCollectionEntry
	// which are themselves WITH_EDITORONLY_DATA. Their callers (CompactSharedMesh /
	// CompactSharedLevel) are body-guarded the same way.
	//
	// SortKey implementations live in Helpers/PCGExCollectionSortKeys.{h,cpp} as free
	// functions so they're directly testable from PCGExtendedToolkitTest. FArchiveBlake3
	// (the process-stable Blake3 archive that backs the descriptor digest) is exposed in
	// Helpers/PCGExArchiveBlake3.h. The full SortKey contract -- process-stable, fully
	// discriminating, leading-field stable on mesh path -- is documented on the header
	// declarations.

	// --- Policy structs supplying entry-specific knowledge to CompactShared<>. ---
	struct FMeshPolicy
	{
		using EntryType = FPCGExMeshCollectionEntry;
		using CollectionType = UPCGExMeshCollection;

		static FName PinName()
		{
			return PCGExCollections::Labels::MeshesPin;
		}

		static uint32 Hash(const FPCGExMeshCollectionEntry& E)
		{
			return MeshContentHash(E);
		}

		static bool Equals(const FPCGExMeshCollectionEntry& A, const FPCGExMeshCollectionEntry& B)
		{
			return MeshContentEquals(A, B);
		}

		static FString SortKey(const FPCGExMeshCollectionEntry& E)
		{
			return ::PCGExSharedCompact::MeshSortKey(E);
		}

		static const TArray<FPCGExMeshCollectionEntry>& Contributions(const FPCGExPCGDataAssetCollectionEntry& E)
		{
			return E.EditorMeshContributions;
		}

		static const TArray<FInstancedStruct>& InheritedDefaults(const FPCGExPCGDataAssetCollectionEntry& E)
		{
			return E.EditorMeshInheritedDefaults;
		}

		static const TArray<int32>& LocalPicks(const FPCGExPCGDataAssetCollectionEntry& E)
		{
			return E.EditorLocalPicks;
		}

		static int16 UnpackSec(int32 Packed, int32& OutLocal)
		{
			int16 Sec;
			FPCGExLevelExportContext::UnpackLocalPick(Packed, OutLocal, Sec);
			return Sec;
		}
	};

	struct FLevelPolicy
	{
		using EntryType = FPCGExLevelCollectionEntry;
		using CollectionType = UPCGExLevelCollection;

		static FName PinName()
		{
			return PCGExCollections::Labels::LevelsPin;
		}

		static uint32 Hash(const FPCGExLevelCollectionEntry& E)
		{
			return GetTypeHash(E.Level.ToSoftObjectPath());
		}

		static bool Equals(const FPCGExLevelCollectionEntry& A, const FPCGExLevelCollectionEntry& B)
		{
			return A.Level.ToSoftObjectPath() == B.Level.ToSoftObjectPath();
		}

		static FString SortKey(const FPCGExLevelCollectionEntry& E)
		{
			return ::PCGExSharedCompact::LevelSortKey(E);
		}

		static const TArray<FPCGExLevelCollectionEntry>& Contributions(const FPCGExPCGDataAssetCollectionEntry& E)
		{
			return E.EditorLevelContributions;
		}

		// Level entries don't carry property-component data, so there's no inherited-defaults
		// view to aggregate. Returning an empty static makes the templated CompactShared body
		// produce an empty aggregate that flows through to RefreshCollectionPropertiesFromEntries
		// as the "no opinion, fall through to contributors" signal.
		static const TArray<FInstancedStruct>& InheritedDefaults(const FPCGExPCGDataAssetCollectionEntry& /*E*/)
		{
			static const TArray<FInstancedStruct> Empty;
			return Empty;
		}

		static const TArray<int32>& LocalPicks(const FPCGExPCGDataAssetCollectionEntry& E)
		{
			return E.EditorLevelLocalPicks;
		}

		static int16 UnpackSec(int32 Packed, int32& OutLocal)
		{
			OutLocal = Packed;
			return -1;
		}
	};

	// Merge every entry's local contributions into one deduplicated shared collection
	// (identity via TPolicy::Hash + TPolicy::Equals), then rewrite Tag_EntryIdx on the
	// policy's pin against the resulting shared indices. Tags/Category/PropertyOverrides
	// on existing shared entries are preserved across rebuilds when identity survives.
	// Deterministic ordering: by content-derived SortKey ascending -- stable across cold cooks
	// and editor restarts (does NOT depend on the per-process FName comparison-index hash).
	template <typename TPolicy>
	static void CompactShared(
		UObject* Outer,
		TArray<FPCGExPCGDataAssetCollectionEntry>& Entries,
		TObjectPtr<typename TPolicy::CollectionType>& SharedCollectionRef,
		TSoftObjectPtr<typename TPolicy::CollectionType>* ExternalFallback)
	{
		using TEntry = TPolicy::EntryType;
		using TCollection = TPolicy::CollectionType;

		// Skip everything when there's nothing to merge AND no existing shared state to clear.
		// Avoids a synchronous external-asset load on unrelated edits (e.g. weight tweak on a
		// non-level entry) when no entry contributes to this policy.
		bool bHasContributions = false;
		for (const FPCGExPCGDataAssetCollectionEntry& E : Entries)
		{
			if (TPolicy::Contributions(E).Num() > 0)
			{
				bHasContributions = true;
				break;
			}
		}
		if (!bHasContributions && !SharedCollectionRef
			&& (!ExternalFallback || ExternalFallback->IsNull()))
		{
			return;
		}

		// External mode: if the in-memory ref was nulled by a prior externalization, pull the
		// asset back into the collection package as the working buffer so the preserved-fields
		// pass below sees the previous entries' user edits (Tags/Category/PropertyOverrides).
		// The next ExternalizeSharedAndActorCollections will overwrite the same external uasset.
		if (!SharedCollectionRef && ExternalFallback && !ExternalFallback->IsNull())
		{
			if (TCollection* Loaded = ExternalFallback->LoadSynchronous())
			{
				Loaded->Rename(nullptr, Outer, REN_DontCreateRedirectors | REN_NonTransactional);
				SharedCollectionRef = Loaded;
			}
		}

		// Reuse the same UObject across calls so its CollectionGUID -- baked into every
		// per-entry Tag_EntryIdx -- stays stable. Replacing it would invalidate on-disk hashes.
		if (!SharedCollectionRef)
		{
			SharedCollectionRef = NewObject<TCollection>(Outer);
		}

		struct FPreserved
		{
			TEntry Identity;
			TSet<FName> Tags;
			FName Category = NAME_None;
			FPCGExPropertyOverrides PropertyOverrides;
		};
		TMap<uint32, TArray<FPreserved>> PreservedByHash;
		for (const TEntry& E : SharedCollectionRef->Entries)
		{
			const uint32 H = TPolicy::Hash(E);
			FPreserved& P = PreservedByHash.FindOrAdd(H).AddDefaulted_GetRef();
			P.Identity = E;
			P.Tags = E.Tags;
			P.Category = E.Category;
			P.PropertyOverrides = E.PropertyOverrides;
		}

		struct FGroup
		{
			uint32 Hash = 0;
			const TEntry* Representative = nullptr;
			FString SortKey;
			TArray<TPair<int32, int32>> Contributors; // (entryIdx, localContribIdx)
			int32 WeightSum = 0;
		};
		TMap<uint32, TArray<FGroup>> HashBuckets;

		for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); EntryIdx++)
		{
			const TArray<TEntry>& Contribs = TPolicy::Contributions(Entries[EntryIdx]);
			for (int32 LocalIdx = 0; LocalIdx < Contribs.Num(); LocalIdx++)
			{
				const TEntry& Contrib = Contribs[LocalIdx];
				const uint32 H = TPolicy::Hash(Contrib);

				TArray<FGroup>& Bucket = HashBuckets.FindOrAdd(H);
				FGroup* Match = nullptr;
				for (FGroup& G : Bucket)
				{
					if (TPolicy::Equals(*G.Representative, Contrib))
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
					NewGroup.SortKey = TPolicy::SortKey(Contrib);
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
		// Order purely by the content-derived, process-stable SortKey. FGroup::Hash is NOT used
		// here: it comes from TPolicy::Hash -> GetTypeHash(FSoftObjectPath) -> GetTypeHash(FName),
		// which is the per-process FName comparison index and reshuffles across sessions/cooks.
		// TPolicy::SortKey fully discriminates every distinct group (see FMeshPolicy::SortKey), so
		// no two groups share a key and there is no tie-break to fall back on. Hash is retained
		// only as an in-process bucket key for grouping/preservation above.
		AllGroups.Sort([](const FGroup& A, const FGroup& B)
		{
			return A.SortKey < B.SortKey;
		});

		TArray<TEntry> MergedEntries;
		MergedEntries.Reserve(AllGroups.Num());

		TArray<TArray<int32>> LocalToSharedByEntry;
		LocalToSharedByEntry.SetNum(Entries.Num());
		for (int32 i = 0; i < Entries.Num(); i++)
		{
			// -1 = no shared mapping; rewrite pass leaves the hash unwritten.
			LocalToSharedByEntry[i].Init(-1, TPolicy::Contributions(Entries[i]).Num());
		}

		for (int32 SharedIdx = 0; SharedIdx < AllGroups.Num(); SharedIdx++)
		{
			const FGroup& G = AllGroups[SharedIdx];
			TEntry Merged = *G.Representative;
			Merged.Weight = FMath::Max(1, G.WeightSum);

			if (const TArray<FPreserved>* Bucket = PreservedByHash.Find(G.Hash))
			{
				for (const FPreserved& P : *Bucket)
				{
					if (TPolicy::Equals(P.Identity, *G.Representative))
					{
						Merged.Tags = P.Tags;
						Merged.Category = P.Category;
						// PropertyOverrides is intentionally NOT preserved: it's derived from
						// per-export actor contributions, not user-authored on the shared
						// collection. Preserving it perpetuates stale values across re-exports.
						// Tags/Category ARE user-authored and have no per-export contributor.
						break;
					}
				}
			}

			MergedEntries.Add(MoveTemp(Merged));

			for (const TPair<int32, int32>& C : G.Contributors)
			{
				LocalToSharedByEntry[C.Key][C.Value] = SharedIdx;
			}
		}

		SharedCollectionRef->Entries = MoveTemp(MergedEntries);

		// Aggregate the per-export "inherited defaults" views across every contributing entry.
		// Each entry's view is the actor-class CDO/asset-fallback aggregate computed by the
		// exporter (FPCGExLevelExportContext::MeshInheritedDefaults). Per property name, only
		// values unanimously agreed across entries survive; disagreements drop out and fall
		// through to per-entry contributors at merge time. Level-policy returns an empty array
		// so this aggregate is naturally empty for the level path.
		TArray<TConstArrayView<FInstancedStruct>> InheritedViews;
		InheritedViews.Reserve(Entries.Num());
		for (const FPCGExPCGDataAssetCollectionEntry& E : Entries)
		{
			InheritedViews.Emplace(TPolicy::InheritedDefaults(E));
		}
		TArray<FInstancedStruct> InheritedDefaultsAggregate = PCGExProperties::AggregateAgreedValuesByName(InheritedViews);

		// Rebuild CollectionProperties from the union of every merged entry's enabled
		// PropertyOverrides. Mesh entries authored by UPCGExDefaultLevelDataExporter carry
		// their source actor's property-component values in their overrides; this collapses
		// them into a canonical schema on the shared collection and re-syncs the per-entry
		// overrides against it. No-op for collection types whose entries don't carry
		// property data (e.g. UPCGExLevelCollection in current usage).
		SharedCollectionRef->RefreshCollectionPropertiesFromEntries(
			EPCGExSchemaMergePolicy::StrictTypeMatch,
			InheritedDefaultsAggregate);

		SharedCollectionRef->RebuildStagingData(true);

		// Per-entry Tag_EntryIdx rewrite. CollectionMap is rebuilt separately so it can
		// register the other shared collection too. Sequential: UPCGMetadata mutation is
		// not safe on worker threads in editor flow.
		PCGExCollections::FPickPacker Packer;
		Packer.RegisterCollection(SharedCollectionRef);

		const FName PinName = TPolicy::PinName();
		for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); EntryIdx++)
		{
			FPCGExPCGDataAssetCollectionEntry& Entry = Entries[EntryIdx];
			if (!Entry.ExportedDataAsset)
			{
				continue;
			}

			UPCGBasePointData* PD = FindPointDataByPin(Entry.ExportedDataAsset, PinName);
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
			const TArray<int32>& LocalPicks = TPolicy::LocalPicks(Entry);
			const int32 N = FMath::Min(LocalPicks.Num(), MetaEntries.Num());
			for (int32 i = 0; i < N; i++)
			{
				const int32 Packed = LocalPicks[i];
				if (Packed == -1)
				{
					continue;
				}
				int32 LocalIdx;
				const int16 Sec = TPolicy::UnpackSec(Packed, LocalIdx);
				if (!LocalToShared.IsValidIndex(LocalIdx))
				{
					continue;
				}
				const int32 SharedIdx = LocalToShared[LocalIdx];
				if (SharedIdx < 0)
				{
					continue;
				}
				const uint64 Hash = Packer.GetPickIdx(SharedCollectionRef, static_cast<int16>(SharedIdx), Sec);
				EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash));
			}
		}
	}
#endif // WITH_EDITOR
}

#pragma endregion

#pragma region UPCGExPCGDataAssetCollection

void UPCGExPCGDataAssetCollection::CompactSharedMesh()
{
#if WITH_EDITORONLY_DATA
	PCGExSharedCompact::CompactShared<PCGExSharedCompact::FMeshPolicy>(
		this, Entries, SharedMeshCollection,
		IsExternalActive() ? &ExternalSharedMeshCollection : nullptr);
#endif
}

void UPCGExPCGDataAssetCollection::CompactSharedLevel()
{
#if WITH_EDITORONLY_DATA
	PCGExSharedCompact::CompactShared<PCGExSharedCompact::FLevelPolicy>(
		this, Entries, SharedLevelCollection,
		IsExternalActive() ? &ExternalSharedLevelCollection : nullptr);
#endif
}

void UPCGExPCGDataAssetCollection::RebuildCollectionMaps()
{
	for (FPCGExPCGDataAssetCollectionEntry& Entry : Entries)
	{
		if (!Entry.ExportedDataAsset)
		{
			continue;
		}

		PCGExCollections::FPickPacker FullPacker;
		if (SharedMeshCollection)
		{
			FullPacker.RegisterCollection(SharedMeshCollection);
		}
		if (SharedLevelCollection)
		{
			FullPacker.RegisterCollection(SharedLevelCollection);
		}
		if (Entry.EmbeddedActorCollection)
		{
			FullPacker.RegisterCollection(Entry.EmbeddedActorCollection);
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

void UPCGExPCGDataAssetCollection::ExternalizeSharedAndActorCollections()
{
#if WITH_EDITOR
	if (!IsExternalActive())
	{
		return;
	}

	// Naming uses the collection's GUID for cross-collection uniqueness in a shared export
	// folder, and is short enough to stay within filesystem path budgets. GUID is stable
	// across rebuilds -- filenames are reused (P4-friendly overwrites).
	const FString FolderPath = ExportFolder.Path;
	const FString GuidPrefix = GetExternalAssetPrefix();

	if (SharedMeshCollection)
	{
		const FString AssetName = GuidPrefix + TEXT("_Meshes");
		ExternalSharedMeshCollection = PCGExSharedCompact::ExternalizeUObject(SharedMeshCollection, FolderPath / AssetName, AssetName);
	}

	if (SharedLevelCollection)
	{
		const FString AssetName = GuidPrefix + TEXT("_Levels");
		ExternalSharedLevelCollection = PCGExSharedCompact::ExternalizeUObject(SharedLevelCollection, FolderPath / AssetName, AssetName);
	}

	// Per-entry actor collections. Done before RebuildCollectionMaps so the soft paths the
	// packer bakes into the CollectionMap pin already point at the external assets.
	for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); EntryIdx++)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = Entries[EntryIdx];
		if (!Entry.EmbeddedActorCollection)
		{
			continue;
		}

		const FString AssetName = FString::Printf(TEXT("%s_E%03d_Actors"), *GuidPrefix, EntryIdx);
		Entry.ExternalActorCollection = PCGExSharedCompact::ExternalizeUObject(Entry.EmbeddedActorCollection, FolderPath / AssetName, AssetName);
	}
#endif
}

void UPCGExPCGDataAssetCollection::ExternalizeExportedDataAssets()
{
#if WITH_EDITOR
	if (!IsExternalActive())
	{
		return;
	}

	const FString FolderPath = ExportFolder.Path;
	const FString GuidPrefix = GetExternalAssetPrefix();

	for (int32 EntryIdx = 0; EntryIdx < Entries.Num(); EntryIdx++)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = Entries[EntryIdx];
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

void UPCGExPCGDataAssetCollection::InternalizeSubobjects()
{
#if WITH_EDITOR
	// Pull each externalized subobject back into the collection's package and null the
	// soft refs. Used on External -> Embedded toggle. CompactShared's load-back path
	// also rehydrates shared collections lazily, but per-entry assets need explicit
	// internalization here because they have no equivalent build-time fallback.
	using namespace PCGExSharedCompact;

	Internalize(SharedMeshCollection, ExternalSharedMeshCollection, this);
	Internalize(SharedLevelCollection, ExternalSharedLevelCollection, this);

	for (FPCGExPCGDataAssetCollectionEntry& Entry : Entries)
	{
		Internalize(Entry.EmbeddedActorCollection, Entry.ExternalActorCollection, this);
		Internalize(Entry.ExportedDataAsset, Entry.ExternalExportedDataAsset, this);
		if (Entry.ExportedDataAsset)
		{
			Entry.Staging.Path = FSoftObjectPath(Entry.ExportedDataAsset);
		}
	}
#endif
}

void UPCGExPCGDataAssetCollection::SaveExternalPackages()
{
#if WITH_EDITOR
	// TSet dedup is defensive -- Shared* and per-entry packages are distinct by GUID-prefixed
	// name, but unrelated future callers could legitimately produce duplicates.
	TSet<UPackage*> Packages;

	auto AddPackageFor = [&Packages](UObject* Obj)
	{
		if (!Obj)
		{
			return;
		}
		if (UPackage* Pkg = Obj->GetOutermost())
		{
			if (Pkg != GetTransientPackage())
			{
				Packages.Add(Pkg);
			}
		}
	};

	AddPackageFor(SharedMeshCollection);
	AddPackageFor(SharedLevelCollection);
	for (const FPCGExPCGDataAssetCollectionEntry& Entry : Entries)
	{
		AddPackageFor(Entry.EmbeddedActorCollection);
		AddPackageFor(Entry.ExportedDataAsset);
	}

	for (UPackage* Pkg : Packages)
	{
		if (Pkg == GetOutermost())
		{
			continue;
		} // never re-enter saving ourselves

		const FString FileName = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		UPackage::SavePackage(Pkg, nullptr, *FileName, SaveArgs);
	}
#endif
}

void UPCGExPCGDataAssetCollection::RebuildSharedCollections()
{
	CompactSharedMesh();
	CompactSharedLevel();

	// Externalize Shared* + per-entry actor collections BEFORE the CollectionMap is baked
	// so the soft paths recorded in the map already point at their external packages.
	// Externalize* methods short-circuit internally when !IsExternalActive().
	ExternalizeSharedAndActorCollections();

	RebuildCollectionMaps();

	// Externalize ExportedDataAsset AFTER the map is baked. The map lives inside
	// ExportedDataAsset as an inner UPCGParamData and moves with the rename; the
	// FSoftObjectPath values it carries are by-value and unaffected.
	ExternalizeExportedDataAssets();
}

void UPCGExPCGDataAssetCollection::Serialize(FArchive& Ar)
{
	// External mode: the Instanced fields are working buffers during the editor session -- their
	// targets live in separate external packages once externalization has run. Without this
	// scrub, UE's Instanced-property serializer would chase those pointers and bake hard
	// references into our saved package, eagerly loading every external asset alongside us
	// and defeating the lazy-load goal. Soft refs + Staging.Path remain the on-disk addresses.
	// Skipped for transacting (undo/redo) so the transaction buffer can round-trip the in-memory
	// state without loss.
	if (IsExternalActive() && Ar.IsSaving() && !Ar.IsTransacting())
	{
		const TObjectPtr<UPCGExMeshCollection> KeepMesh = SharedMeshCollection;
		const TObjectPtr<UPCGExLevelCollection> KeepLevel = SharedLevelCollection;
		TArray<TObjectPtr<UPCGDataAsset>> KeepData;
		TArray<TObjectPtr<UPCGExActorCollection>> KeepActors;
		KeepData.Reserve(Entries.Num());
		KeepActors.Reserve(Entries.Num());

		for (FPCGExPCGDataAssetCollectionEntry& E : Entries)
		{
			KeepData.Add(E.ExportedDataAsset);
			KeepActors.Add(E.EmbeddedActorCollection);
			E.ExportedDataAsset = nullptr;
			E.EmbeddedActorCollection = nullptr;
		}
		SharedMeshCollection = nullptr;
		SharedLevelCollection = nullptr;

		Super::Serialize(Ar);

		SharedMeshCollection = KeepMesh;
		SharedLevelCollection = KeepLevel;
		for (int32 i = 0; i < Entries.Num(); i++)
		{
			Entries[i].ExportedDataAsset = KeepData[i];
			Entries[i].EmbeddedActorCollection = KeepActors[i];
		}
		return;
	}

	Super::Serialize(Ar);
}

void UPCGExPCGDataAssetCollection::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// On a real duplicate (not PIE), both this collection AND its outered shared collections
	// receive new CollectionGUIDs while per-entry ExportedDataAssets still carry hashes keyed
	// to the ORIGINAL shared GUIDs. Re-stamp them so the duplicate is consistent without a
	// manual rebuild.
	if (!bDuplicateForPIE)
	{
		RebuildSharedCollections();
	}
}

void UPCGExPCGDataAssetCollection::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// Cook-time safety net for users who edited a source level and cooked without a manual
	// rebuild (or recovered from a mid-edit editor crash). Idempotent.
	//
	// Only re-bake the IN-MEMORY state here. 
	// We deliberately do NOT call SaveExternalPackages() during cook:
	//  - SavePackage(Pkg, nullptr, ...) is an editor (uncooked) save that writes the SOURCE
	//    .uasset under /Content/. A cook must be read-only w.r.t. source content; writing it
	//    dirties the workspace and fails / forces a writable-flip on read-only (Perforce) files.
	//  - It mutates packages the cooker may have already cooked (no effect) or not yet cooked
	//    (changing the source mid-cook), making the output depend on cook scheduling.
	//  - In concurrent / cook-by-the-book saving, GIsSavingPackage is held for the whole batch;
	//    a nested non-concurrent SavePackage clears it on scope-exit, breaking that invariant.
	// On most cases (except for potential multi-threaded cooks), external objects are
	// cooked from the loaded in-memory ones which should be enough.
	// Actually might want to consider just removing this as the levels aren't actually being re-harvested
	// at all here so the safety net is not quite comprehensive in the first place.
	if (ObjectSaveContext.IsCooking())
	{
		RebuildSharedCollections();
	}
	Super::PreSave(ObjectSaveContext);
}

#if WITH_EDITOR

void UPCGExPCGDataAssetCollection::EDITOR_OnPostStagingRebuild()
{
	RebuildSharedCollections();
}

void UPCGExPCGDataAssetCollection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const bool bToggledExternal = (PropName == GET_MEMBER_NAME_CHECKED(UPCGExPCGDataAssetCollection, bUseExternalAssets));
	const bool bChangedFolder = (PropName == GET_MEMBER_NAME_CHECKED(UPCGExPCGDataAssetCollection, ExportFolder));

	if (bToggledExternal && !bUseExternalAssets)
	{
		// External -> Embedded: pull subobjects back into the collection package, null soft
		// refs, then re-bake the CollectionMap with the now-inner paths.
		InternalizeSubobjects();
		RebuildSharedCollections();
	}
	else if (bToggledExternal || bChangedFolder)
	{
		// Embedded -> External, or External folder moved: re-externalize using the
		// (new) settings. CompactShared's load-back rehydrates the working buffers.
		RebuildSharedCollections();
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

void UPCGExPCGDataAssetCollection::GetCookDependencyAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	// Base = each entry's Staging.Path. For Level-sourced entries that path is the embedded
	// (or repointed external) ExportedDataAsset; for DataAsset-sourced entries it's the
	// user-referenced UPCGDataAsset.
	Super::GetCookDependencyAssetPaths(OutPaths);

	// Embedded shared collections live in this asset's package so the package itself is
	// already in the cook -- but their *entries* hold soft refs to the actual meshes /
	// levels which cook traversal won't reach on its own.
	if (SharedMeshCollection)
	{
		SharedMeshCollection->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
	}
	if (SharedLevelCollection)
	{
		SharedLevelCollection->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
	}

	// Externalized shared collections sit in their own packages -- surface them so the
	// ModifyCook scan force-cooks those packages too. Once cooked, our registry scan
	// re-enters them (they're UPCGExAssetCollection subclasses and implement the interface),
	// so their leaf soft refs follow automatically.
	if (!ExternalSharedMeshCollection.IsNull())
	{
		OutPaths.Add(ExternalSharedMeshCollection.ToSoftObjectPath());
	}
	if (!ExternalSharedLevelCollection.IsNull())
	{
		OutPaths.Add(ExternalSharedLevelCollection.ToSoftObjectPath());
	}

	// Per-entry actor collections (embedded + external). Hang off the entry as hard
	// subobjects rather than entries[], so the base walk skips them.
	for (const FPCGExPCGDataAssetCollectionEntry& Entry : Entries)
	{
		if (Entry.EmbeddedActorCollection)
		{
			Entry.EmbeddedActorCollection->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
		}
		if (!Entry.ExternalActorCollection.IsNull())
		{
			OutPaths.Add(Entry.ExternalActorCollection.ToSoftObjectPath());
		}
	}
}
#endif

#pragma endregion
