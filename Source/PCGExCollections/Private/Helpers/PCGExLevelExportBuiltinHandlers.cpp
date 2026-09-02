// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExLevelExportBuiltinHandlers.h"

#if WITH_EDITOR

#include "PCGExPropertyCollectionComponent.h"
#include "PCGExSchemaMerging.h"
#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Descriptors/PCGExComponentDescriptors.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "Helpers/PCGExCollectionSortKeys.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "Helpers/PCGExLevelExportShared.h"
#include "ISMPartition/ISMComponentDescriptor.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Serialization/ArchiveCrc32.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveAdapters.h"

#pragma region Mesh

namespace PCGExMeshExportHandler
{
	// Components share an entry only when they agree on mesh, source kind, AND the descriptor
	// fingerprint (every UPROPERTY except OverrideMaterials -- those become per-entry variants).
	// PropertyComponentHash is folded in so two mesh actors that author distinct property-component
	// values land in distinct entries.
	struct FMeshEntryKey
	{
		FSoftObjectPath MeshPath;
		uint32 DescriptorFingerprint = 0;
		bool bIsISMSource = false;
		uint32 PropertyComponentHash = 0;

		bool operator==(const FMeshEntryKey& Other) const
		{
			return MeshPath == Other.MeshPath
				&& DescriptorFingerprint == Other.DescriptorFingerprint
				&& bIsISMSource == Other.bIsISMSource
				&& PropertyComponentHash == Other.PropertyComponentHash;
		}

		friend uint32 GetTypeHash(const FMeshEntryKey& Key)
		{
			return HashCombine(
				HashCombine(
					HashCombine(GetTypeHash(Key.MeshPath), Key.DescriptorFingerprint),
					Key.bIsISMSource ? 1u : 0u),
				Key.PropertyComponentHash);
		}
	};

	// Per-export cache of (Resolved schema, Hash) keyed by source actor. ExtractSchemaFromActor is the
	// expensive step and its result is read twice per actor: to bucket by hash and to seed overrides.
	struct FActorPropertySchemaCache
	{
		TArray<FInstancedStruct> Resolved;
		uint32 Hash = 0;
	};

	struct FScratch final : FPCGExExportScratch
	{
		TMap<AActor*, FActorPropertySchemaCache> PropertySchemaCache;

		// Parallel to the writer's entries.
		TArray<FMeshEntryKey> Keys;
		TArray<TMap<uint32, int32>> VariantHashToIndex;
	};

	// Hash is over the EFFECTIVE extracted schema (BP class chain + PreparePropertyValues), not raw
	// component state -- a raw-state hash would collapse actors that resolve to different values.
	const FActorPropertySchemaCache& GetOrComputeActorPropertySchema(AActor* Actor, TMap<AActor*, FActorPropertySchemaCache>& Cache)
	{
		if (const FActorPropertySchemaCache* Found = Cache.Find(Actor))
		{
			return *Found;
		}
		FActorPropertySchemaCache& Entry = Cache.Add(Actor);

		if (!Actor || !UPCGExPropertyCollectionComponent::FindOnActor(Actor))
		{
			return Entry;
		}

		Entry.Resolved = UPCGExPropertyCollectionComponent::ExtractSchemaFromActor(Actor);
		if (Entry.Resolved.IsEmpty())
		{
			return Entry;
		}

		FArchiveCrc32 Crc;
		for (const FInstancedStruct& Prop : Entry.Resolved)
		{
			const UScriptStruct* ScriptStruct = Prop.GetScriptStruct();
			if (!ScriptStruct)
			{
				continue;
			}

			// Type path prefix so two values of different types but matching byte layout don't alias.
			FString TypeName = ScriptStruct->GetPathName();
			Crc << TypeName;

			const_cast<UScriptStruct*>(ScriptStruct)->SerializeItem(
				FStructuredArchiveFromArchive(Crc).GetSlot(),
				const_cast<uint8*>(Prop.GetMemory()), nullptr);
		}
		Entry.Hash = Crc.GetCrc();
		return Entry;
	}

	uint32 HashMaterials(const UStaticMeshComponent* Comp)
	{
		uint32 H = 0;
		for (int32 i = 0; i < Comp->GetNumOverrideMaterials(); i++)
		{
			if (UMaterialInterface* M = Comp->GetMaterial(i))
			{
				H = HashCombine(H, GetTypeHash(FSoftObjectPath(M)));
			}
		}
		return H;
	}

	// Variant index for the component's override set on the given entry; -1 = mesh defaults.
	int32 TrackMaterialVariant(const UStaticMeshComponent* Comp, FPCGExMeshCollectionEntry& Entry, TMap<uint32, int32>& VariantHashToIndex)
	{
		const uint32 MatHash = HashMaterials(Comp);
		if (MatHash == 0)
		{
			return -1;
		}

		if (const int32* Existing = VariantHashToIndex.Find(MatHash))
		{
			return *Existing;
		}

		const int32 VariantIdx = Entry.MaterialOverrideVariantsList.Num();
		VariantHashToIndex.Add(MatHash, VariantIdx);

		Entry.MaterialVariants = EPCGExMaterialVariantsMode::Multi;
		FPCGExMaterialOverrideCollection& Variant = Entry.MaterialOverrideVariantsList.AddDefaulted_GetRef();
		Variant.Weight = 1;
		for (int32 SlotIdx = 0; SlotIdx < Comp->GetNumOverrideMaterials(); SlotIdx++)
		{
			FPCGExMaterialOverrideEntry& MatEntry = Variant.Overrides.AddDefaulted_GetRef();
			MatEntry.SlotIndex = SlotIdx;
			if (UMaterialInterface* M = Comp->GetMaterial(SlotIdx))
			{
				MatEntry.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(M));
			}
		}

		return VariantIdx;
	}

	// Non-const ref because FSoftISMComponentDescriptor's copy ctor is explicit. OverrideMaterials are
	// stripped transiently: they go on the entry as variants and must not influence identity.
	template <typename TDescriptor>
	uint32 FingerprintDescriptor(TDescriptor& Descriptor)
	{
		decltype(Descriptor.OverrideMaterials) SavedMaterials = MoveTemp(Descriptor.OverrideMaterials);
		Descriptor.OverrideMaterials.Reset();

		FArchiveCrc32 CrcArchive;
		TDescriptor::StaticStruct()->SerializeBin(CrcArchive, &Descriptor);
		const uint32 Crc = CrcArchive.GetCrc();

		Descriptor.OverrideMaterials = MoveTemp(SavedMaterials);
		return Crc;
	}

	// Identity hash deliberately omits Weight/Tags/Category/PropertyOverrides -- Weight accumulates from
	// contributors, the rest are user-owned on the shared entry. Descriptors are not hashed (large
	// structs); Equals resolves collisions.
	uint32 ContentHash(const FPCGExMeshCollectionEntry& E)
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

		H = HashCombine(H, E.PropertyComponentHash);
		return H;
	}

	bool MaterialOverrideEquals(const FPCGExMaterialOverrideSingleEntry& A, const FPCGExMaterialOverrideSingleEntry& B)
	{
		return A.Weight == B.Weight && A.Material.ToSoftObjectPath() == B.Material.ToSoftObjectPath();
	}

	bool MaterialOverrideEquals(const FPCGExMaterialOverrideEntry& A, const FPCGExMaterialOverrideEntry& B)
	{
		return A.SlotIndex == B.SlotIndex && A.Material.ToSoftObjectPath() == B.Material.ToSoftObjectPath();
	}

	bool MaterialOverrideEquals(const FPCGExMaterialOverrideCollection& A, const FPCGExMaterialOverrideCollection& B)
	{
		if (A.Weight != B.Weight || A.Overrides.Num() != B.Overrides.Num())
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

	bool ContentEquals(const FPCGExMeshCollectionEntry& A, const FPCGExMeshCollectionEntry& B)
	{
		if (A.StaticMesh.ToSoftObjectPath() != B.StaticMesh.ToSoftObjectPath()
			|| A.MaterialVariants != B.MaterialVariants
			|| A.SlotIndex != B.SlotIndex
			|| A.DescriptorSource != B.DescriptorSource
			|| A.PropertyComponentHash != B.PropertyComponentHash)
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
		return true;
	}

	class FPolicy final : public IPCGExExportSlotPolicy
	{
	public:
		virtual uint32 Hash(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return ContentHash(static_cast<const FPCGExMeshCollectionEntry&>(Entry));
		}

		virtual bool Equals(const FPCGExAssetCollectionEntry& A, const FPCGExAssetCollectionEntry& B) const override
		{
			return ContentEquals(static_cast<const FPCGExMeshCollectionEntry&>(A), static_cast<const FPCGExMeshCollectionEntry&>(B));
		}

		virtual FString SortKey(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return PCGExSharedCompact::MeshSortKey(static_cast<const FPCGExMeshCollectionEntry&>(Entry));
		}

		// Loose identity for EntryId preservation -- the binding survives variant/descriptor tweaks.
		virtual FSoftObjectPath PrimaryPath(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return static_cast<const FPCGExMeshCollectionEntry&>(Entry).StaticMesh.ToSoftObjectPath();
		}
	};
}

FPCGExExportSlotDesc UPCGExMeshExportHandler::GetSlotDesc() const
{
	FPCGExExportSlotDesc Desc;
	Desc.SlotId = PCGExLevelExport::Slots::Meshes;
	Desc.PinName = PCGExCollections::Labels::MeshesPin;
	Desc.CollectionClass = UPCGExMeshCollection::StaticClass();
	Desc.EntryStruct = FPCGExMeshCollectionEntry::StaticStruct();
	Desc.Scope = EPCGExExportSlotScope::Shared;
	Desc.bSupportsSecondary = true;
	return Desc;
}

TSharedPtr<const IPCGExExportSlotPolicy> UPCGExMeshExportHandler::MakePolicy() const
{
	return MakeShared<PCGExMeshExportHandler::FPolicy>();
}

// Mesh and Actor classifications are mutually exclusive -- Actor-classified actors with ISMCs are
// intentionally NOT harvested here. Bounds are the mesh's intrinsic local AABB; the bounds evaluator is
// not consulted because component variation belongs on per-entry descriptor data.
void UPCGExMeshExportHandler::Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const
{
	using namespace PCGExMeshExportHandler;

	if (Candidate.ClaimedSlot != PCGExLevelExport::Slots::Meshes)
	{
		return;
	}

	const UPCGExDefaultLevelDataExporter* Default = Cast<UPCGExDefaultLevelDataExporter>(Exporter);
	const bool bCaptureMaterialOverrides = Default ? Default->bCaptureMaterialOverrides : true;

	AActor* Actor = Candidate.Actor;
	FScratch& Scratch = Writer.GetOrCreateScratch<FScratch>();

	// Property-component identity is computed once per actor and folded into every mesh entry this actor
	// contributes to, so actors authoring distinct property values land in distinct buckets.
	const FActorPropertySchemaCache& Schema = GetOrComputeActorPropertySchema(Actor, Scratch.PropertySchemaCache);

	TInlineComponentArray<UStaticMeshComponent*> SMCs;
	Actor->GetComponents<UStaticMeshComponent>(SMCs);

	for (UStaticMeshComponent* SMC : SMCs)
	{
		if (!SMC)
		{
			continue;
		}

		UStaticMesh* Mesh = SMC->GetStaticMesh();
		if (!Mesh)
		{
			continue;
		}

		UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC);
		const bool bIsISM = ISMC != nullptr;
		const int32 InstanceCount = bIsISM ? ISMC->GetInstanceCount() : 1;
		if (InstanceCount == 0)
		{
			continue;
		}

		FMeshEntryKey Key;
		Key.MeshPath = FSoftObjectPath(Mesh);
		Key.bIsISMSource = bIsISM;
		Key.PropertyComponentHash = Schema.Hash;

		FSoftISMComponentDescriptor TentativeISM;
		FPCGExStaticMeshComponentDescriptor TentativeSM;

		if (bIsISM)
		{
			TentativeISM.InitFrom(SMC, /*bInitBodyInstance=*/false);
			Key.DescriptorFingerprint = FingerprintDescriptor(TentativeISM);
		}
		else
		{
			TentativeSM.InitFrom(SMC, /*bInitBodyInstance=*/false);
			Key.DescriptorFingerprint = FingerprintDescriptor(TentativeSM);
		}

		// When capturing, variants are the sole material carrier: sec=-1 must mean mesh defaults, so the
		// stored descriptor must not bake one contributor's overrides.
		if (bCaptureMaterialOverrides)
		{
			TentativeISM.OverrideMaterials.Reset();
			TentativeSM.OverrideMaterials.Reset();
		}

		const int32 LocalIdx = Writer.FindOrAddEntry(
			GetTypeHash(Key),
			[&Scratch, &Key](const int32 Index, const FPCGExAssetCollectionEntry&) { return Scratch.Keys[Index] == Key; },
			[&](FPCGExAssetCollectionEntry& Base)
			{
				FPCGExMeshCollectionEntry& Entry = static_cast<FPCGExMeshCollectionEntry&>(Base);
				Entry.StaticMesh = TSoftObjectPtr<UStaticMesh>(Key.MeshPath);
				Entry.PropertyComponentHash = Key.PropertyComponentHash;
				// First contribution stores the descriptor; later contributors share the fingerprint by
				// construction, so the stored value is canonical.
				if (bIsISM)
				{
					Entry.ISMDescriptor = MoveTemp(TentativeISM);
				}
				else
				{
					Entry.SMDescriptor = MoveTemp(TentativeSM);
				}

				// All contributors to this bucket share the property hash, so the first actor's resolved
				// values are canonical. Outer identity seeded so the entry ships in SyncToSchema shape.
				if (!Schema.Resolved.IsEmpty())
				{
					Entry.PropertyOverrides.Overrides.Reset(Schema.Resolved.Num());
					for (const FInstancedStruct& Prop : Schema.Resolved)
					{
						FPCGExPropertyOverrideEntry& Slot = Entry.PropertyOverrides.Overrides.AddDefaulted_GetRef();
						Slot.Value = Prop;
						Slot.bEnabled = true;
#if WITH_EDITORONLY_DATA
						Slot.SeedOuterIdentityFromInner();
#endif
					}
				}

				Scratch.Keys.Add(Key);
				Scratch.VariantHashToIndex.AddDefaulted();
			},
			InstanceCount);

		FPCGExMeshCollectionEntry& Entry = Writer.GetEntry<FPCGExMeshCollectionEntry>(LocalIdx);
		const int16 VariantIdx = bCaptureMaterialOverrides
			? static_cast<int16>(TrackMaterialVariant(SMC, Entry, Scratch.VariantHashToIndex[LocalIdx]))
			: static_cast<int16>(-1);

		const FBox MeshBounds = Mesh->GetBoundingBox();

		if (bIsISM)
		{
			Writer.Items.Reserve(Writer.Items.Num() + InstanceCount);
			for (int32 Idx = 0; Idx < InstanceCount; Idx++)
			{
				FTransform InstanceWorld;
				ISMC->GetInstanceTransform(Idx, InstanceWorld, /*bWorldSpace=*/true);
				Writer.AddItem(Source.ToFrame(InstanceWorld), MeshBounds.Min, MeshBounds.Max, Actor, LocalIdx, VariantIdx);
			}
		}
		else
		{
			Writer.AddItem(Source.ToFrame(SMC->GetComponentTransform()), MeshBounds.Min, MeshBounds.Max, Actor, LocalIdx, VariantIdx);
		}
	}
}

// "Common-ancestor" inherited-defaults view of the contributing actors: per property, the value all
// unique BP classes agree on at the CDO level, else the asset's authored default. Seeds the shared mesh
// collection's CollectionProperties without falling back to whichever per-instance override came first.
void UPCGExMeshExportHandler::FinalizeSlot(FPCGExExportSlotWriter& Writer, UObject* Outer, const UPCGExLevelDataExporter* Exporter) const
{
	using namespace PCGExMeshExportHandler;

	FScratch& Scratch = Writer.GetOrCreateScratch<FScratch>();

	TMap<UClass*, TArray<FInstancedStruct>> InheritedByClass;
	TMap<UClass*, TArray<FInstancedStruct>> AssetDefaultsByClass;
	for (const TPair<AActor*, FActorPropertySchemaCache>& Pair : Scratch.PropertySchemaCache)
	{
		AActor* Actor = Pair.Key;
		if (!Actor)
		{
			continue;
		}
		UClass* Class = Actor->GetClass();
		if (InheritedByClass.Contains(Class))
		{
			continue;
		}
		UPCGExPropertyCollectionComponent* Comp = UPCGExPropertyCollectionComponent::FindOnActor(Actor);
		if (!Comp)
		{
			continue;
		}
		InheritedByClass.Add(Class, Comp->BuildInheritedSchema());
		AssetDefaultsByClass.Add(Class, Comp->BuildAssetDefaultSchema());
	}

	TArray<TConstArrayView<FInstancedStruct>> InheritedViews;
	InheritedViews.Reserve(InheritedByClass.Num());
	for (const TPair<UClass*, TArray<FInstancedStruct>>& Pair : InheritedByClass)
	{
		InheritedViews.Emplace(Pair.Value);
	}
	TArray<TConstArrayView<FInstancedStruct>> AssetDefaultViews;
	AssetDefaultViews.Reserve(AssetDefaultsByClass.Num());
	for (const TPair<UClass*, TArray<FInstancedStruct>>& Pair : AssetDefaultsByClass)
	{
		AssetDefaultViews.Emplace(Pair.Value);
	}
	Writer.InheritedDefaults = PCGExProperties::AggregateAgreedValuesByName(InheritedViews, AssetDefaultViews);
}

void UPCGExMeshExportHandler::WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
{
	FPCGMetadataAttribute<FSoftObjectPath>* MeshAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, true);
	if (!MeshAttr)
	{
		return;
	}
	for (int32 i = 0; i < Writer.Items.Num(); i++)
	{
		const int32 LocalIdx = Writer.Items[i].LocalEntryIndex;
		if (LocalIdx >= 0)
		{
			MeshAttr->SetValue(MetaEntries[i], Writer.GetEntry<FPCGExMeshCollectionEntry>(LocalIdx).StaticMesh.ToSoftObjectPath());
		}
	}
}

#pragma endregion

#pragma region Actor

namespace PCGExActorExportHandler
{
	struct FActorInstanceKey
	{
		FSoftClassPath ClassPath;
		uint32 DeltaHash = 0;

		bool operator==(const FActorInstanceKey& Other) const
		{
			return ClassPath == Other.ClassPath && DeltaHash == Other.DeltaHash;
		}

		friend uint32 GetTypeHash(const FActorInstanceKey& Key)
		{
			return HashCombine(GetTypeHash(Key.ClassPath), Key.DeltaHash);
		}
	};

	struct FScratch final : FPCGExExportScratch
	{
		// Parallel to the writer's entries. The representative is the first actor of the bucket -- the one
		// whose authored state the stored delta reflects; every member shares the delta hash, so any is a
		// valid donor for the property-component scan.
		TArray<FActorInstanceKey> Keys;
		TArray<AActor*> Representatives;
	};

	uint32 EntryHash(const FPCGExActorCollectionEntry& E)
	{
		return HashCombine(GetTypeHash(FSoftClassPath(E.Actor.ToString())), PCGExActorDelta::HashDelta(E.SerializedPropertyDelta));
	}

	class FPolicy final : public IPCGExExportSlotPolicy
	{
	public:
		virtual uint32 Hash(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return EntryHash(static_cast<const FPCGExActorCollectionEntry&>(Entry));
		}

		virtual bool Equals(const FPCGExAssetCollectionEntry& A, const FPCGExAssetCollectionEntry& B) const override
		{
			const FPCGExActorCollectionEntry& EA = static_cast<const FPCGExActorCollectionEntry&>(A);
			const FPCGExActorCollectionEntry& EB = static_cast<const FPCGExActorCollectionEntry&>(B);
			return EA.Actor.ToSoftObjectPath() == EB.Actor.ToSoftObjectPath() && EA.SerializedPropertyDelta == EB.SerializedPropertyDelta;
		}

		virtual FString SortKey(const FPCGExAssetCollectionEntry& Entry) const override
		{
			const FPCGExActorCollectionEntry& E = static_cast<const FPCGExActorCollectionEntry&>(Entry);
			return FString::Printf(TEXT("%s|%08X"), *E.Actor.ToString(), PCGExActorDelta::HashDelta(E.SerializedPropertyDelta));
		}

		// Class alone: the binding survives a delta change.
		virtual FSoftObjectPath PrimaryPath(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return static_cast<const FPCGExActorCollectionEntry&>(Entry).Actor.ToSoftObjectPath();
		}
	};
}

FPCGExExportSlotDesc UPCGExActorExportHandler::GetSlotDesc() const
{
	FPCGExExportSlotDesc Desc;
	Desc.SlotId = PCGExLevelExport::Slots::Actors;
	Desc.PinName = PCGExCollections::Labels::ActorsPin;
	Desc.CollectionClass = UPCGExActorCollection::StaticClass();
	Desc.EntryStruct = FPCGExActorCollectionEntry::StaticStruct();
	Desc.Scope = EPCGExExportSlotScope::PerEntry;
	return Desc;
}

TSharedPtr<const IPCGExExportSlotPolicy> UPCGExActorExportHandler::MakePolicy() const
{
	return MakeShared<PCGExActorExportHandler::FPolicy>();
}

void UPCGExActorExportHandler::Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const
{
	using namespace PCGExActorExportHandler;

	if (Candidate.ClaimedSlot != PCGExLevelExport::Slots::Actors)
	{
		return;
	}

	const UPCGExDefaultLevelDataExporter* Default = Cast<UPCGExDefaultLevelDataExporter>(Exporter);
	const bool bCaptureDeltas = Default ? (Default->bCapturePropertyDeltas && Default->bGenerateCollections) : false;
	const EPCGExValueTagMode ValueTagMode = Default ? Default->ValueTagMode : EPCGExValueTagMode::NoParsing;
	const UPCGExBoundsEvaluator* BoundsEvaluator = Default ? Default->BoundsEvaluator.Get() : nullptr;

	AActor* Actor = Candidate.Actor;
	FScratch& Scratch = Writer.GetOrCreateScratch<FScratch>();

	TArray<uint8> DeltaBytes;
	TArray<FSoftObjectPath> DeltaCollaterals;
	uint32 DeltaHash = 0;
	if (bCaptureDeltas)
	{
		DeltaBytes = PCGExActorDelta::SerializeActorDelta(Actor, &DeltaCollaterals);
		DeltaHash = PCGExActorDelta::HashDelta(DeltaBytes);
	}

	FActorInstanceKey Key;
	Key.ClassPath = FSoftClassPath(Actor->GetClass());
	Key.DeltaHash = DeltaHash;

	// Raw Actor->Tags feed the property delta; the entry's Tags are the parse-mode effective set,
	// intersected across the bucket.
	const TSet<FName> EffectiveTags = PCGExLevelExportShared::BuildEffectiveTags(Actor, ValueTagMode);

	bool bAdded = false;
	const int32 LocalIdx = Writer.FindOrAddEntry(
		GetTypeHash(Key),
		[&Scratch, &Key](const int32 Index, const FPCGExAssetCollectionEntry&) { return Scratch.Keys[Index] == Key; },
		[&](FPCGExAssetCollectionEntry& Base)
		{
			FPCGExActorCollectionEntry& Entry = static_cast<FPCGExActorCollectionEntry&>(Base);
			Entry.Actor = TSoftClassPtr<AActor>(Key.ClassPath);
			Entry.Tags = EffectiveTags;
			if (!DeltaBytes.IsEmpty())
			{
				Entry.SerializedPropertyDelta = MoveTemp(DeltaBytes);
				Entry.DeltaCollateralPaths = MoveTemp(DeltaCollaterals);
			}
			Scratch.Keys.Add(Key);
			Scratch.Representatives.Add(Actor);
			bAdded = true;
		});

	if (!bAdded)
	{
		FPCGExActorCollectionEntry& Entry = Writer.GetEntry<FPCGExActorCollectionEntry>(LocalIdx);
		Entry.Tags = Entry.Tags.Intersect(EffectiveTags);
	}

	FTransform Transform;
	FVector BoundsMin, BoundsMax;
	PCGExLevelExportShared::EvaluateActorItem(Actor, Source, BoundsEvaluator, Transform, BoundsMin, BoundsMax);
	Writer.AddItem(Transform, BoundsMin, BoundsMax, Actor, LocalIdx);
}

// Per-actor tag names as a joined string. Only meaningful in NoParsing and ParseAndKeep modes; under
// Parse every tag is already a typed attribute.
void UPCGExActorExportHandler::WriteItemAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
{
	const UPCGExDefaultLevelDataExporter* Default = Cast<UPCGExDefaultLevelDataExporter>(Exporter);
	if (!Default || !Default->bWriteInstanceTags || Default->InstanceTagsAttributeName == NAME_None || Default->ValueTagMode == EPCGExValueTagMode::Parse)
	{
		return;
	}

	FPCGMetadataAttribute<FString>* InstanceTagsAttr = Meta->CreateAttribute<FString>(Default->InstanceTagsAttributeName, FString(), false, true);
	if (!InstanceTagsAttr)
	{
		return;
	}

	for (int32 i = 0; i < Writer.Items.Num(); i++)
	{
		const FString TagsStr = PCGExLevelExportShared::BuildInstanceTagString(Writer.Items[i].SourceActor, Default->ValueTagMode);
		if (!TagsStr.IsEmpty())
		{
			InstanceTagsAttr->SetValue(MetaEntries[i], TagsStr);
		}
	}
}

void UPCGExActorExportHandler::WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
{
	FPCGMetadataAttribute<FSoftClassPath>* ActorClassAttr = Meta->CreateAttribute<FSoftClassPath>(TEXT("ActorClass"), FSoftClassPath(), false, true);
	if (!ActorClassAttr)
	{
		return;
	}
	for (int32 i = 0; i < Writer.Items.Num(); i++)
	{
		ActorClassAttr->SetValue(MetaEntries[i], FSoftClassPath(Writer.Items[i].SourceActor->GetClass()));
	}
}

// Scan UPCGExPropertyCollectionComponent on each representative actor and merge into the embedded
// collection's schema + per-entry overrides. Runs BEFORE the staging rebuild so it observes the final
// schema. The exporter's policy is mirrored onto the collection so a manual rebuild uses the same one.
void UPCGExActorExportHandler::FinalizeEmbeddedCollection(UPCGExAssetCollection* Collection, FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
{
	using namespace PCGExActorExportHandler;

	UPCGExActorCollection* ActorCollection = Cast<UPCGExActorCollection>(Collection);
	if (!ActorCollection)
	{
		return;
	}

	const UPCGExDefaultLevelDataExporter* Default = Cast<UPCGExDefaultLevelDataExporter>(Exporter);
	const EPCGExSchemaMergePolicy Policy = Default ? Default->SchemaMergePolicy : EPCGExSchemaMergePolicy::StrictTypeMatch;

	FScratch& Scratch = Writer.GetOrCreateScratch<FScratch>();
	ActorCollection->SchemaMergePolicy = Policy;
	ActorCollection->RebuildPropertiesFromActorComponents(Policy, Scratch.Representatives);
}

#pragma endregion

#pragma region LevelInstance

namespace PCGExLevelInstanceExportHandler
{
	class FPolicy final : public IPCGExExportSlotPolicy
	{
	public:
		virtual uint32 Hash(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return GetTypeHash(static_cast<const FPCGExLevelCollectionEntry&>(Entry).Level.ToSoftObjectPath());
		}

		virtual bool Equals(const FPCGExAssetCollectionEntry& A, const FPCGExAssetCollectionEntry& B) const override
		{
			return static_cast<const FPCGExLevelCollectionEntry&>(A).Level.ToSoftObjectPath() == static_cast<const FPCGExLevelCollectionEntry&>(B).Level.ToSoftObjectPath();
		}

		virtual FString SortKey(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return PCGExSharedCompact::LevelSortKey(static_cast<const FPCGExLevelCollectionEntry&>(Entry));
		}

		virtual FSoftObjectPath PrimaryPath(const FPCGExAssetCollectionEntry& Entry) const override
		{
			return static_cast<const FPCGExLevelCollectionEntry&>(Entry).Level.ToSoftObjectPath();
		}
	};
}

FPCGExExportSlotDesc UPCGExLevelInstanceExportHandler::GetSlotDesc() const
{
	FPCGExExportSlotDesc Desc;
	Desc.SlotId = PCGExLevelExport::Slots::Levels;
	Desc.PinName = PCGExCollections::Labels::LevelsPin;
	Desc.CollectionClass = UPCGExLevelCollection::StaticClass();
	Desc.EntryStruct = FPCGExLevelCollectionEntry::StaticStruct();
	Desc.Scope = EPCGExExportSlotScope::Shared;
	return Desc;
}

TSharedPtr<const IPCGExExportSlotPolicy> UPCGExLevelInstanceExportHandler::MakePolicy() const
{
	return MakeShared<PCGExLevelInstanceExportHandler::FPolicy>();
}

void UPCGExLevelInstanceExportHandler::Collect(const FPCGExExportCandidate& Candidate, const FPCGExLevelExportSource& Source, const UPCGExLevelDataExporter* Exporter, FPCGExExportSlotWriter& Writer) const
{
	if (Candidate.ClaimedSlot != PCGExLevelExport::Slots::Levels)
	{
		return;
	}

	const ALevelInstance* LI = Cast<ALevelInstance>(Candidate.Actor);
	if (!LI)
	{
		return;
	}
	const FSoftObjectPath LevelPath = LI->GetWorldAsset().ToSoftObjectPath();
	if (LevelPath.IsNull())
	{
		return;
	}

	const UPCGExDefaultLevelDataExporter* Default = Cast<UPCGExDefaultLevelDataExporter>(Exporter);
	const UPCGExBoundsEvaluator* BoundsEvaluator = Default ? Default->BoundsEvaluator.Get() : nullptr;

	const int32 LocalIdx = Writer.FindOrAddEntry(
		GetTypeHash(LevelPath),
		[&LevelPath](const int32, const FPCGExAssetCollectionEntry& Entry) { return static_cast<const FPCGExLevelCollectionEntry&>(Entry).Level.ToSoftObjectPath() == LevelPath; },
		[&LevelPath](FPCGExAssetCollectionEntry& Entry) { static_cast<FPCGExLevelCollectionEntry&>(Entry).Level = TSoftObjectPtr<UWorld>(LevelPath); });

	FTransform Transform;
	FVector BoundsMin, BoundsMax;
	PCGExLevelExportShared::EvaluateActorItem(Candidate.Actor, Source, BoundsEvaluator, Transform, BoundsMin, BoundsMax);
	Writer.AddItem(Transform, BoundsMin, BoundsMax, Candidate.Actor, LocalIdx);
}

void UPCGExLevelInstanceExportHandler::WriteRawAttributes(UPCGMetadata* Meta, TConstArrayView<int64> MetaEntries, const FPCGExExportSlotWriter& Writer, const UPCGExLevelDataExporter* Exporter) const
{
	FPCGMetadataAttribute<FSoftObjectPath>* LevelAssetAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("LevelAsset"), FSoftObjectPath(), false, true);
	if (!LevelAssetAttr)
	{
		return;
	}
	for (int32 i = 0; i < Writer.Items.Num(); i++)
	{
		const int32 LocalIdx = Writer.Items[i].LocalEntryIndex;
		if (LocalIdx >= 0)
		{
			LevelAssetAttr->SetValue(MetaEntries[i], Writer.GetEntry<FPCGExLevelCollectionEntry>(LocalIdx).Level.ToSoftObjectPath());
		}
	}
}

#pragma endregion

#pragma region Registration

namespace PCGExLevelExport
{
	void RegisterBuiltinHandlers()
	{
		FHandlerRegistry& Registry = FHandlerRegistry::Get();
		Registry.Register(UPCGExMeshExportHandler::StaticClass());
		Registry.Register(UPCGExActorExportHandler::StaticClass());
		Registry.Register(UPCGExLevelInstanceExportHandler::StaticClass());
	}

	void UnregisterBuiltinHandlers()
	{
		FHandlerRegistry& Registry = FHandlerRegistry::Get();
		Registry.Unregister(Slots::Meshes);
		Registry.Unregister(Slots::Actors);
		Registry.Unregister(Slots::Levels);
	}

	bool IsBuiltinHandlerClass(const UClass* HandlerClass)
	{
		return HandlerClass
			&& (HandlerClass->IsChildOf<UPCGExMeshExportHandler>()
				|| HandlerClass->IsChildOf<UPCGExActorExportHandler>()
				|| HandlerClass->IsChildOf<UPCGExLevelInstanceExportHandler>());
	}
}

#pragma endregion

#endif // WITH_EDITOR
