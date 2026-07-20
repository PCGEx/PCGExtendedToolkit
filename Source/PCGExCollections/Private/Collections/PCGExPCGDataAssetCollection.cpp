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
#include "Helpers/PCGExCollectionExternalization.h"
#include "Helpers/PCGExCollectionSortKeys.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExLevelDataExporter.h"


// Static-init type registration: TypeId=PCGDataAsset, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(PCGDataAsset, UPCGExPCGDataAssetCollection, FPCGExPCGDataAssetCollectionEntry, "PCG Data Asset Collection", Base)

// Machinery registration: TypeId -> globals block + state class. Colocated with the type
// (moved here from PCGExOmniCollection.cpp in Phase C1 -- the typed collection owns a
// UPCGExPCGDataTypeState itself now, so the capability data lives where the machinery does;
// third-party types with machinery register theirs the same way, in their own module).
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
		// Level export depends on machinery the host must run -- in hosts that don't,
		// stage nothing and point at the composition path. Capability query so future
		// host kinds (per-type processor seam) only change the helper.
		if (!UPCGExPCGDataAssetCollection::HostSupportsDataAssetMachinery(OwningCollection))
		{
			UE_LOG(LogPCGEx, Warning,
			       TEXT("Level-sourced PCGDataAsset entry ('%s') is hosted by a collection without the level-export machinery -- entry skipped. Author it in a PCGDataAsset collection and reference that collection as a subcollection entry instead."),
			       *Level.ToSoftObjectPath().ToString());

			// Discard any embedded export carried in via a cross-asset copy: foreign hosts
			// never consume it, and a pointer to another asset's private subobject must not
			// survive a save. Own subobjects get the transient-rename (mirrors the recreate
			// path below); foreign-owned pointers are only nulled.
			auto DiscardEmbedded = [OwningCollection](auto& Embedded)
			{
				if (!Embedded)
				{
					return;
				}
				if (Embedded->GetOuter() == OwningCollection)
				{
					Embedded->Rename(nullptr, GetTransientPackage(),
					                 REN_DontCreateRedirectors | REN_NonTransactional);
				}
				Embedded = nullptr;
			};
			DiscardEmbedded(ExportedDataAsset);
			DiscardEmbedded(EmbeddedActorCollection);

			Staging.Bounds = FBox(ForceInit);
			Staging.Path = FSoftObjectPath();
#if WITH_EDITORONLY_DATA
			EditorMeshContributions.Reset();
			EditorMeshInheritedDefaults.Reset();
			EditorLocalPicks.Reset();
			EditorLevelContributions.Reset();
			EditorLevelLocalPicks.Reset();
#endif
			FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
			return;
		}

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
		// The previous actor collection is the exporter's working buffer -- its CollectionGUID
		// and EntryIds are bound by external references (variants). Cold external sessions load
		// the externalized asset back. The entry ref stays assigned during export so the object
		// stays GC-reachable.
		if (!EmbeddedActorCollection && !ExternalActorCollection.IsNull())
		{
			PCGExHelpers::LoadBlocking_AnyThreadTpl(ExternalActorCollection);
			EmbeddedActorCollection = ExternalActorCollection.Get();
		}
		ExportContext.PreviousActorCollection = EmbeddedActorCollection;
		ExportContext.ActorCollectionOut = &EmbeddedActorCollection;

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

			// Failed exports return before ActorCollectionOut is assigned; the previous
			// collection's outer chain was just retired -- never serialize it.
			EmbeddedActorCollection = nullptr;
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

	// ExternalizeUObject + Internalize live in Helpers/PCGExCollectionExternalization.h now
	// (shared with the Valency bonding-rules externalization). See that header for contracts.

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
			return MeshSortKey(E);
		}

		// Loose identity for EntryId preservation -- the binding survives variant/descriptor tweaks.
		static FSoftObjectPath PrimaryPath(const FPCGExMeshCollectionEntry& E)
		{
			return E.StaticMesh.ToSoftObjectPath();
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
			return LevelSortKey(E);
		}

		// Path IS the level identity; present to keep the templated preservation body uniform.
		static FSoftObjectPath PrimaryPath(const FPCGExLevelCollectionEntry& E)
		{
			return E.Level.ToSoftObjectPath();
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
		const TArray<FPCGExPCGDataAssetCollectionEntry*>& Entries,
		TObjectPtr<typename TPolicy::CollectionType>& SharedCollectionRef,
		TSoftObjectPtr<typename TPolicy::CollectionType>* ExternalFallback)
	{
		using TEntry = TPolicy::EntryType;
		using TCollection = TPolicy::CollectionType;

		// Skip everything when there's nothing to merge AND no existing shared state to clear.
		// Avoids a synchronous external-asset load on unrelated edits (e.g. weight tweak on a
		// non-level entry) when no entry contributes to this policy.
		bool bHasContributions = false;
		for (const FPCGExPCGDataAssetCollectionEntry* E : Entries)
		{
			if (TPolicy::Contributions(*E).Num() > 0)
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
			bool bEntryIdConsumed = false; // exact-matched by a merged group; keeps its id out of the loose-fallback bank
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
			const TArray<TEntry>& Contribs = TPolicy::Contributions(*Entries[EntryIdx]);
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
			LocalToSharedByEntry[i].Init(-1, TPolicy::Contributions(*Entries[i]).Num());
		}

		for (int32 SharedIdx = 0; SharedIdx < AllGroups.Num(); SharedIdx++)
		{
			const FGroup& G = AllGroups[SharedIdx];
			TEntry Merged = *G.Representative;
			Merged.Weight = FMath::Max(1, G.WeightSum);

			// Contribution snapshots carry their SOURCE's id, not this collection's; zero is
			// also the "not preserved" marker for the loose pass below.
			Merged.EntryId = 0;

			if (TArray<FPreserved>* Bucket = PreservedByHash.Find(G.Hash))
			{
				for (FPreserved& P : *Bucket)
				{
					if (TPolicy::Equals(P.Identity, *G.Representative))
					{
						Merged.Tags = P.Tags;
						Merged.Category = P.Category;
						// PropertyOverrides is intentionally NOT preserved: it's derived from
						// per-export actor contributions, not user-authored on the shared
						// collection. Preserving it perpetuates stale values across re-exports.
						// Tags/Category ARE user-authored and have no per-export contributor.

						// EntryId IS preserved: external references bind by id.
						Merged.EntryId = P.Identity.EntryId;
						P.bEntryIdConsumed = true;
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
						FallbackIds.Deposit(0, GetTypeHash(TPolicy::PrimaryPath(P.Identity)), P.Identity.EntryId);
					}
				}
			}

			for (TEntry& Merged : MergedEntries)
			{
				if (Merged.EntryId == 0)
				{
					Merged.EntryId = FallbackIds.ClaimLoose(GetTypeHash(TPolicy::PrimaryPath(Merged)));
				}
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
		for (const FPCGExPCGDataAssetCollectionEntry* E : Entries)
		{
			InheritedViews.Emplace(TPolicy::InheritedDefaults(*E));
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
			FPCGExPCGDataAssetCollectionEntry& Entry = *Entries[EntryIdx];
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

UPCGExPCGDataAssetCollection::UPCGExPCGDataAssetCollection()
{
	// Always-present machinery state (Phase C1). Default subobject: delta-serializes against
	// the CDO's, inherits RF_Transactional from the asset (RF_PropagateToSubObjects), and
	// guarantees the nested "External Storage" details block is never None.
	MachineryState = CreateDefaultSubobject<UPCGExPCGDataTypeState>(TEXT("MachineryState"));
}

void UPCGExPCGDataAssetCollection::PostLoad()
{
	Super::PostLoad();

	// Phase C1 migration (2026-07-19): the machinery members moved into MachineryState.
	// The deprecated slots still LOAD legacy data (UHT registers them under their unsuffixed
	// names with CPF_Deprecated: tagged properties match, saves always skip) -- move the
	// values over once and clear. Referenced subobjects keep their outer (this collection),
	// the state only holds the refs -- same shape as an Omni host.
	if (MachineryState)
	{
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
		if (SharedMeshCollection_DEPRECATED)
		{
			MachineryState->SharedMeshCollection = SharedMeshCollection_DEPRECATED;
			SharedMeshCollection_DEPRECATED = nullptr;
		}
		if (SharedLevelCollection_DEPRECATED)
		{
			MachineryState->SharedLevelCollection = SharedLevelCollection_DEPRECATED;
			SharedLevelCollection_DEPRECATED = nullptr;
		}
		if (!ExternalSharedMeshCollection_DEPRECATED.IsNull())
		{
			MachineryState->ExternalSharedMeshCollection = ExternalSharedMeshCollection_DEPRECATED;
			ExternalSharedMeshCollection_DEPRECATED.Reset();
		}
		if (!ExternalSharedLevelCollection_DEPRECATED.IsNull())
		{
			MachineryState->ExternalSharedLevelCollection = ExternalSharedLevelCollection_DEPRECATED;
			ExternalSharedLevelCollection_DEPRECATED.Reset();
		}
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
	// registered type-state capability (per-type processor seam, Phase B).
	return Host && Host->SupportsTypeMachinery(PCGExAssetCollection::TypeIds::PCGDataAsset);
}

FString UPCGExPCGDataAssetCollection::MakeExternalAssetPrefixFor(const UPCGExAssetCollection* Host)
{
	return FString::Printf(TEXT("G_%08X"), Host ? Host->GetCollectionGUID() : 0u);
}

void UPCGExPCGDataAssetCollection::CompactSharedMeshFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITORONLY_DATA
	if (!State.IsValid())
	{
		return;
	}
	PCGExSharedCompact::CompactShared<PCGExSharedCompact::FMeshPolicy>(
		State.Host, State.Entries, *State.SharedMeshCollection,
		State.bExternalActive ? State.ExternalSharedMeshCollection : nullptr);
#endif
}

void UPCGExPCGDataAssetCollection::CompactSharedLevelFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITORONLY_DATA
	if (!State.IsValid())
	{
		return;
	}
	PCGExSharedCompact::CompactShared<PCGExSharedCompact::FLevelPolicy>(
		State.Host, State.Entries, *State.SharedLevelCollection,
		State.bExternalActive ? State.ExternalSharedLevelCollection : nullptr);
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
		if (*State.SharedMeshCollection)
		{
			FullPacker.RegisterCollection(*State.SharedMeshCollection);
		}
		if (*State.SharedLevelCollection)
		{
			FullPacker.RegisterCollection(*State.SharedLevelCollection);
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

void UPCGExPCGDataAssetCollection::ExternalizeSharedAndActorCollectionsFor(FPCGExPCGDataAssetMachinery& State)
{
#if WITH_EDITOR
	if (!State.IsValid() || !State.bExternalActive)
	{
		return;
	}

	// Naming uses the collection's GUID for cross-collection uniqueness in a shared export
	// folder, and is short enough to stay within filesystem path budgets. GUID is stable
	// across rebuilds -- filenames are reused (P4-friendly overwrites).
	const FString& FolderPath = State.ExportFolderPath;
	const FString& GuidPrefix = State.ExternalAssetPrefix;

	if (*State.SharedMeshCollection)
	{
		const FString AssetName = GuidPrefix + TEXT("_Meshes");
		*State.ExternalSharedMeshCollection = PCGExSharedCompact::ExternalizeUObject(*State.SharedMeshCollection, FolderPath / AssetName, AssetName);
	}

	if (*State.SharedLevelCollection)
	{
		const FString AssetName = GuidPrefix + TEXT("_Levels");
		*State.ExternalSharedLevelCollection = PCGExSharedCompact::ExternalizeUObject(*State.SharedLevelCollection, FolderPath / AssetName, AssetName);
	}

	// Per-entry actor collections. Done before RebuildCollectionMaps so the soft paths the
	// packer bakes into the CollectionMap pin already point at the external assets.
	for (int32 EntryIdx = 0; EntryIdx < State.Entries.Num(); EntryIdx++)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = *State.Entries[EntryIdx];
		if (!Entry.EmbeddedActorCollection)
		{
			continue;
		}

		const FString AssetName = FString::Printf(TEXT("%s_E%03d_Actors"), *GuidPrefix, EntryIdx);
		Entry.ExternalActorCollection = PCGExSharedCompact::ExternalizeUObject(Entry.EmbeddedActorCollection, FolderPath / AssetName, AssetName);
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
	// soft refs. Used on External -> Embedded toggle. CompactShared's load-back path
	// also rehydrates shared collections lazily, but per-entry assets need explicit
	// internalization here because they have no equivalent build-time fallback.
	using namespace PCGExSharedCompact;

	Internalize(*State.SharedMeshCollection, *State.ExternalSharedMeshCollection, State.Host);
	Internalize(*State.SharedLevelCollection, *State.ExternalSharedLevelCollection, State.Host);

	for (FPCGExPCGDataAssetCollectionEntry* EntryPtr : State.Entries)
	{
		FPCGExPCGDataAssetCollectionEntry& Entry = *EntryPtr;
		Internalize(Entry.EmbeddedActorCollection, Entry.ExternalActorCollection, State.Host);
		Internalize(Entry.ExportedDataAsset, Entry.ExternalExportedDataAsset, State.Host);
		if (Entry.ExportedDataAsset)
		{
			Entry.Staging.Path = FSoftObjectPath(Entry.ExportedDataAsset);
		}
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

	AddPackageFor(*State.SharedMeshCollection);
	AddPackageFor(*State.SharedLevelCollection);
	for (const FPCGExPCGDataAssetCollectionEntry* Entry : State.Entries)
	{
		AddPackageFor(Entry->EmbeddedActorCollection);
		AddPackageFor(Entry->ExportedDataAsset);
	}

	for (UPackage* Pkg : Packages)
	{
		if (Pkg == State.Host->GetOutermost())
		{
			continue;
		} // never re-enter saving the host itself

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
	CompactSharedMeshFor(State);
	CompactSharedLevelFor(State);

	// Externalize Shared* + per-entry actor collections BEFORE the CollectionMap is baked
	// so the soft paths recorded in the map already point at their external packages.
	// Externalize* cores short-circuit internally when external mode isn't active.
	ExternalizeSharedAndActorCollectionsFor(State);

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

void UPCGExPCGDataAssetCollection::ScrubSharedRefsForSave(TObjectPtr<UPCGExMeshCollection>& MeshSlot, TObjectPtr<UPCGExLevelCollection>& LevelSlot, FPCGExPCGDataSharedScrubKeep& OutKeep)
{
	OutKeep.MeshSlot = &MeshSlot;
	OutKeep.LevelSlot = &LevelSlot;
	OutKeep.KeptMesh = MeshSlot;
	OutKeep.KeptLevel = LevelSlot;
	MeshSlot = nullptr;
	LevelSlot = nullptr;
}

void UPCGExPCGDataAssetCollection::RestoreSharedRefsAfterSave(FPCGExPCGDataSharedScrubKeep& Keep)
{
	if (Keep.MeshSlot)
	{
		*Keep.MeshSlot = Keep.KeptMesh;
	}
	if (Keep.LevelSlot)
	{
		*Keep.LevelSlot = Keep.KeptLevel;
	}
	Keep = FPCGExPCGDataSharedScrubKeep();
}

void UPCGExPCGDataAssetCollection::ScrubEntryRefsForSave(const TArray<FPCGExPCGDataAssetCollectionEntry*>& InEntries, FPCGExPCGDataEntryScrubKeep& OutKeep)
{
	OutKeep.Reset();
	OutKeep.Entries.Reserve(InEntries.Num());
	OutKeep.Data.Reserve(InEntries.Num());
	OutKeep.Actors.Reserve(InEntries.Num());

	for (FPCGExPCGDataAssetCollectionEntry* Entry : InEntries)
	{
		OutKeep.Entries.Add(Entry);
		OutKeep.Data.Add(Entry->ExportedDataAsset);
		OutKeep.Actors.Add(Entry->EmbeddedActorCollection);
		Entry->ExportedDataAsset = nullptr;
		Entry->EmbeddedActorCollection = nullptr;
	}
}

void UPCGExPCGDataAssetCollection::RestoreEntryRefsAfterSave(FPCGExPCGDataEntryScrubKeep& Keep)
{
	for (int32 i = 0; i < Keep.Entries.Num(); i++)
	{
		Keep.Entries[i]->ExportedDataAsset = Keep.Data[i];
		Keep.Entries[i]->EmbeddedActorCollection = Keep.Actors[i];
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
	if (MachineryState)
	{
		MachineryState->EDITOR_OnHostPostStagingRebuild(this);
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
	const UPCGExMeshCollection* InSharedMesh,
	const UPCGExLevelCollection* InSharedLevel,
	const TSoftObjectPtr<UPCGExMeshCollection>& InExternalSharedMesh,
	const TSoftObjectPtr<UPCGExLevelCollection>& InExternalSharedLevel,
	const TArray<const FPCGExPCGDataAssetCollectionEntry*>& InEntries,
	TSet<FSoftObjectPath>& OutPaths)
{
	// Embedded shared collections live in the host's package so the package itself is
	// already in the cook -- but their *entries* hold soft refs to the actual meshes /
	// levels which cook traversal won't reach on its own.
	if (InSharedMesh)
	{
		InSharedMesh->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
	}
	if (InSharedLevel)
	{
		InSharedLevel->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
	}

	// Externalized shared collections sit in their own packages -- surface them so the
	// ModifyCook scan force-cooks those packages too. Once cooked, our registry scan
	// re-enters them (they're UPCGExAssetCollection subclasses and implement the interface),
	// so their leaf soft refs follow automatically.
	if (!InExternalSharedMesh.IsNull())
	{
		OutPaths.Add(InExternalSharedMesh.ToSoftObjectPath());
	}
	if (!InExternalSharedLevel.IsNull())
	{
		OutPaths.Add(InExternalSharedLevel.ToSoftObjectPath());
	}

	// Per-entry actor collections (embedded + external). Hang off the entry as hard
	// subobjects rather than entries[], so the base walk skips them.
	for (const FPCGExPCGDataAssetCollectionEntry* Entry : InEntries)
	{
		if (Entry->EmbeddedActorCollection)
		{
			Entry->EmbeddedActorCollection->GetAssetPaths(OutPaths, PCGExAssetCollection::ELoadingFlags::Recursive);
		}
		if (!Entry->ExternalActorCollection.IsNull())
		{
			OutPaths.Add(Entry->ExternalActorCollection.ToSoftObjectPath());
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

	State.SharedMeshCollection = &SharedMeshCollection;
	State.SharedLevelCollection = &SharedLevelCollection;
	State.ExternalSharedMeshCollection = &ExternalSharedMeshCollection;
	State.ExternalSharedLevelCollection = &ExternalSharedLevelCollection;

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
		UPCGExPCGDataAssetCollection::ScrubSharedRefsForSave(SharedMeshCollection, SharedLevelCollection, SharedKeep);

		Super::Serialize(Ar);

		UPCGExPCGDataAssetCollection::RestoreSharedRefsAfterSave(SharedKeep);
	}
	else
	{
		Super::Serialize(Ar);
	}
}

#if WITH_EDITOR

void UPCGExPCGDataTypeState::EDITOR_OnHostPostStagingRebuild(UPCGExAssetCollection* Host)
{
	FPCGExPCGDataAssetMachinery State = MakeMachinery(Host);
	UPCGExPCGDataAssetCollection::RebuildSharedCollectionsFor(State);
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

	UPCGExPCGDataAssetCollection::AppendCookDependencyAssetPathsFor(
		SharedMeshCollection, SharedLevelCollection,
		ExternalSharedMeshCollection, ExternalSharedLevelCollection,
		EntryPtrs, OutPaths);
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
	// Adopt the seed source's external-storage SETTINGS on creation (closes the merge gap:
	// converting/merging a PCGData source into an Omni used to silently fall back to
	// embedded). Only fires on FRESH states -- an existing state is the user's and always
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
	if (!ExternalSharedMeshCollection.IsNull() || !ExternalSharedLevelCollection.IsNull())
	{
		UE_LOG(LogPCGEx, Warning,
		       TEXT("'%s': the removed PCG Data Asset machinery state still pointed at externalized packages ('%s', '%s'). They remain on disk, now orphaned -- delete them manually, or undo, re-add PCGData entries and disable external storage first to internalize them."),
		       Host ? *Host->GetName() : TEXT("<null>"),
		       *ExternalSharedMeshCollection.ToSoftObjectPath().ToString(),
		       *ExternalSharedLevelCollection.ToSoftObjectPath().ToString());
	}
}

#endif

#pragma endregion
