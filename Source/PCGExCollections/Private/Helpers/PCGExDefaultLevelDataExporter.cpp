// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExDefaultLevelDataExporter.h"

#include "PCGDataAsset.h"
#include "PCGParamData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"

#include "Collections/PCGExActorCollection.h"
#include "Collections/PCGExLevelCollection.h"
#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExPCGDataAssetCollection.h"

#include "Helpers/PCGExActorMeshClassificator.h"

#include "UObject/Package.h"

#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

#include "LevelInstance/LevelInstanceActor.h"

#include "PCGExLog.h"
#include "PCGExPropertyCollectionComponent.h"
#include "PCGExSchemaMerging.h"
#include "Data/PCGExDataValue.h"
#include "Data/Descriptors/PCGExComponentDescriptors.h"
#include "Helpers/PCGExActorPropertyDelta.h"
#include "Helpers/PCGExMetaHelpersMacros.h"

#include "ISMPartition/ISMComponentDescriptor.h"
#include "Serialization/ArchiveCrc32.h"

#include "PCGExCollectionsSettingsCache.h"

UPCGExDefaultLevelDataExporter::UPCGExDefaultLevelDataExporter(const FObjectInitializer& ObjectInitializer)
{
	const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;

	UClass* FilterClass = Settings.DefaultContentFilterClass
		? Settings.DefaultContentFilterClass.Get()
		: UPCGExDefaultActorContentFilter::StaticClass();

	UClass* EvalClass = Settings.DefaultBoundsEvaluatorClass
		? Settings.DefaultBoundsEvaluatorClass.Get()
		: UPCGExDefaultBoundsEvaluator::StaticClass();

	UClass* ClassificatorClass = Settings.DefaultMeshClassificatorClass
		? Settings.DefaultMeshClassificatorClass.Get()
		: UPCGExDefaultActorMeshClassificator::StaticClass();

	ContentFilter = Cast<UPCGExActorContentFilter>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("ContentFilter"),
		                                         UPCGExActorContentFilter::StaticClass(), FilterClass, false, false));

	MeshClassificator = Cast<UPCGExActorMeshClassificator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("MeshClassificator"),
		                                         UPCGExActorMeshClassificator::StaticClass(), ClassificatorClass, false, false));

	BoundsEvaluator = Cast<UPCGExBoundsEvaluator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("BoundsEvaluator"),
		                                         UPCGExBoundsEvaluator::StaticClass(), EvalClass, false, false));
}

EPCGExActorExportType UPCGExDefaultLevelDataExporter::ClassifyActor(AActor* Actor) const
{
	// Level instances precede the mesh check: an ALevelInstance can incidentally have
	// a UStaticMeshComponent (gizmo/visualizer) but the meaningful payload is the
	// referenced UWorld asset.
	if (const ALevelInstance* LI = Cast<ALevelInstance>(Actor))
	{
		if (!LI->GetWorldAsset().IsNull())
		{
			return EPCGExActorExportType::Level;
		}
		// Empty world ref -- nothing to embed; fall through to actor-style export so
		// transform/tags survive the round-trip.
	}

	if (MeshClassificator && MeshClassificator->ShouldClassifyAsMesh(Actor))
	{
		// Any UStaticMeshComponent (or subclass -- ISMC, HISM, splines, future kinds)
		// with a valid mesh AND geometry to contribute qualifies the actor as a Mesh
		// container. ISMCs with zero instances contribute nothing and don't count.
		TInlineComponentArray<UStaticMeshComponent*> SMCs;
		Actor->GetComponents<UStaticMeshComponent>(SMCs);
		for (UStaticMeshComponent* SMC : SMCs)
		{
			if (!SMC || !SMC->GetStaticMesh())
			{
				continue;
			}
			if (const UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(SMC))
			{
				if (ISMC->GetInstanceCount() == 0)
				{
					continue;
				}
			}
			return EPCGExActorExportType::Mesh;
		}
	}

	return EPCGExActorExportType::Actor;
}

void UPCGExDefaultLevelDataExporter::OnExportComplete(UPCGDataAsset* OutAsset)
{
	// Default: no-op. Override for custom post-export logic.
}

namespace PCGExDefaultLevelDataExporterInternal
{
	struct FClassifiedActor
	{
		AActor* Actor = nullptr;
		EPCGExActorExportType Type = EPCGExActorExportType::Skip;
		uint32 DeltaHash = 0;
	};

	/** Helper to allocate point data with transforms+bounds, init metadata, and return ranges */
	static UPCGBasePointData* CreatePointData(
		UObject* Outer, int32 NumPoints,
		TPCGValueRange<FTransform>& OutTransforms,
		TPCGValueRange<FVector>& OutBoundsMin,
		TPCGValueRange<FVector>& OutBoundsMax)
	{
		UPCGBasePointData* PointData = NewObject<UPCGPointArrayData>(Outer);
		PCGExPointArrayDataHelpers::SetNumPointsAllocated(
			PointData, NumPoints,
			EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

		OutTransforms = PointData->GetTransformValueRange();
		OutBoundsMin = PointData->GetBoundsMinValueRange();
		OutBoundsMax = PointData->GetBoundsMaxValueRange();

		return PointData;
	}

	static void InitMetadata(UPCGBasePointData* PointData, int32 NumPoints)
	{
		UPCGMetadata* Meta = PointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = PointData->GetMetadataEntryValueRange();

		TArray<TTuple<int64, int64>> DelayedEntries;
		DelayedEntries.SetNum(NumPoints);

		for (int32 i = 0; i < NumPoints; i++)
		{
			MetaEntries[i] = Meta->AddEntryPlaceholder();
			DelayedEntries[i] = MakeTuple(MetaEntries[i], static_cast<int64>(-1));
		}

		Meta->AddDelayedEntries(DelayedEntries);
	}

	static void WorldBoundsToLocal(const FBox& WorldBounds, const FTransform& ActorTransform, FVector& OutBoundsMin, FVector& OutBoundsMax)
	{
		if (WorldBounds.IsValid)
		{
			const FTransform InvTransform = ActorTransform.Inverse();
			const FVector LocalMin = InvTransform.TransformPosition(WorldBounds.Min);
			const FVector LocalMax = InvTransform.TransformPosition(WorldBounds.Max);

			// Re-min/max after transform (rotation can swap axes)
			OutBoundsMin = LocalMin.ComponentMin(LocalMax);
			OutBoundsMax = LocalMin.ComponentMax(LocalMax);
		}
		else
		{
			OutBoundsMin = FVector::ZeroVector;
			OutBoundsMax = FVector::ZeroVector;
		}
	}

	static void WriteActorTransformAndBounds(
		AActor* Actor, int32 Index,
		const UPCGExBoundsEvaluator* Evaluator,
		TPCGValueRange<FTransform>& Transforms,
		TPCGValueRange<FVector>& BoundsMin,
		TPCGValueRange<FVector>& BoundsMax)
	{
		const FTransform ActorTransform = Actor->GetActorTransform();
		Transforms[Index] = ActorTransform;

		const FBox WorldBounds = Evaluator ? Evaluator->EvaluateActorBounds(Actor, nullptr, -1) : FBox(ForceInit);
		WorldBoundsToLocal(WorldBounds, ActorTransform, BoundsMin[Index], BoundsMax[Index]);
	}

	// Components share an entry only when they agree on mesh, source kind, AND the
	// descriptor fingerprint (every UPROPERTY except OverrideMaterials -- those become
	// per-entry variants instead). Different mobility / collision / body instance /
	// light map / etc. → distinct entry, descriptor preserved on each.
	//
	// PropertyComponentHash is folded in so two mesh actors that share mesh+descriptor but
	// author distinct UPCGExPropertyCollectionComponent values land in distinct entries --
	// otherwise their per-instance property data would silently collapse into one bucket.
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

	struct FMeshPoint
	{
		FTransform Transform;
		FVector BoundsMin = FVector::ZeroVector;
		FVector BoundsMax = FVector::ZeroVector;
		FMeshEntryKey EntryKey;
		const UStaticMeshComponent* SourceComponent = nullptr;
		AActor* SourceActor = nullptr;
		int32 MaterialVariantIndex = -1;
	};

	struct FMeshInfo
	{
		int32 EntryIndex = -1;
		int32 TotalCount = 0;
		// Authoritative for the entry; the kind matching the source key is populated,
		// the other stays at engine defaults. Subsequent contributors share the
		// fingerprint by construction, so first-write-wins is a tautology, not a guess.
		FSoftISMComponentDescriptor ISMDescriptor;
		FPCGExStaticMeshComponentDescriptor SMDescriptor;
		TArray<TArray<FSoftObjectPath>> UniqueVariantMaterials;
		TMap<uint32, int32> VariantHashToIndex;

		// Source actor we treat as the representative donor for property-component scanning.
		// Captured on first contribution; all subsequent contributors share the same property
		// hash by construction (it's part of the key), so any one of them is a valid donor.
		TWeakObjectPtr<AActor> RepresentativeActor;
	};

	struct FActorClassInfo
	{
		int32 EntryIndex = -1;
		TSet<FName> IntersectedTags;
		bool bFirstActor = true;
		int32 Count = 0;
		TArray<uint8> SerializedDelta;
		TArray<FSoftObjectPath> CollateralPaths;

		// Live actor we treat as the representative for this (class, delta) bucket. Captured
		// when the first actor of the bucket is seen; carried through so the post-build property-
		// component scan can pull authored values directly from a real instance instead of
		// re-resolving the delta. All actors in the bucket share an identical delta hash, so
		// any one of them is a valid donor; "first wins" matches the SerializedDelta capture.
		TWeakObjectPtr<AActor> RepresentativeActor;
	};

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

	struct FLevelInfo
	{
		int32 EntryIndex = -1;
		int32 Count = 0;
	};

	static uint32 HashMaterials(const UStaticMeshComponent* Comp)
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

	// Per-export-pass cache of (Resolved schema, Hash) keyed by source actor. ExtractSchemaFromActor
	// is the expensive step (BP PreparePropertyValues thunk + class-chain walk + per-entry struct
	// copy) and we need its result twice per actor: once to bucket by PropertyComponentHash, once
	// to seed PropertyOverrides on the bucket representative. Compute once, read twice.
	struct FActorPropertySchemaCache
	{
		TArray<FInstancedStruct> Resolved;
		uint32 Hash = 0;
	};

	// Resolve + hash an actor's property-component schema, memoized by AActor*. Empty/no-component
	// actors land in the cache with Hash=0 and Resolved empty so subsequent lookups are O(1). The
	// hash is over the *effective* extracted schema (matching ExtractSchemaFromActor semantics:
	// BP class chain + PreparePropertyValues), not raw component state -- a raw-state hash would
	// collapse actors that resolve to different values via the chain into the same bucket.
	static const FActorPropertySchemaCache& GetOrComputeActorPropertySchema(
		AActor* Actor,
		TMap<AActor*, FActorPropertySchemaCache>& Cache)
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

	static int32 TrackMaterialVariant(const UStaticMeshComponent* Comp, FMeshInfo& Info)
	{
		if (!Comp)
		{
			return -1;
		}

		const uint32 MatHash = HashMaterials(Comp);
		if (MatHash == 0)
		{
			return -1;
		}

		if (const int32* Existing = Info.VariantHashToIndex.Find(MatHash))
		{
			return *Existing;
		}

		const int32 VariantIdx = Info.UniqueVariantMaterials.Num();
		Info.VariantHashToIndex.Add(MatHash, VariantIdx);

		TArray<FSoftObjectPath>& Mats = Info.UniqueVariantMaterials.AddDefaulted_GetRef();
		for (int32 i = 0; i < Comp->GetNumOverrideMaterials(); i++)
		{
			if (UMaterialInterface* M = Comp->GetMaterial(i))
			{
				Mats.Add(FSoftObjectPath(M));
			}
			else
			{
				Mats.Add(FSoftObjectPath());
			}
		}

		return VariantIdx;
	}

	// Non-const ref because FSoftISMComponentDescriptor's copy ctor is `explicit`,
	// blocking a local-copy form. Save-restore on the original lets us strip
	// OverrideMaterials transiently without mutating observable state -- those go on
	// the entry as variants, so they shouldn't influence descriptor identity.
	template <typename TDescriptor>
	static uint32 FingerprintDescriptor(TDescriptor& Descriptor)
	{
		decltype(Descriptor.OverrideMaterials) SavedMaterials = MoveTemp(Descriptor.OverrideMaterials);
		Descriptor.OverrideMaterials.Reset();

		FArchiveCrc32 CrcArchive;
		TDescriptor::StaticStruct()->SerializeBin(CrcArchive, &Descriptor);
		const uint32 Crc = CrcArchive.GetCrc();

		Descriptor.OverrideMaterials = MoveTemp(SavedMaterials);
		return Crc;
	}

	// Unified mesh-point extraction for a single Mesh-classified actor. Mesh and
	// Actor classifications are mutually exclusive -- Actor-classified actors with
	// ISMCs are intentionally NOT harvested here. Bounds are the mesh's intrinsic
	// local AABB; BoundsEvaluator is not consulted because component variation
	// belongs on per-entry descriptor data, not a coarse per-actor world AABB.
	static void ExtractMeshPointsFromActor(
		AActor* Actor,
		bool bCaptureMaterialOverrides,
		TArray<FMeshPoint>& OutPoints,
		TMap<FMeshEntryKey, FMeshInfo>& InOutMeshInfoMap,
		TMap<AActor*, FActorPropertySchemaCache>& PropertySchemaCache)
	{
		// Property-component identity is computed once per actor and folded into every mesh
		// entry this actor contributes to. Actors that author distinct property values land
		// in distinct mesh buckets so their per-instance PropertyOverrides survive merging.
		const uint32 PropertyComponentHash = GetOrComputeActorPropertySchema(Actor, PropertySchemaCache).Hash;

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

			FMeshEntryKey Key;
			Key.MeshPath = FSoftObjectPath(Mesh);
			Key.bIsISMSource = bIsISM;
			Key.PropertyComponentHash = PropertyComponentHash;

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

			FMeshInfo& Info = InOutMeshInfoMap.FindOrAdd(Key);

			// First contribution stores the descriptor; subsequent contributors are
			// equivalent by fingerprint so the stored value is canonical.
			if (Info.TotalCount == 0)
			{
				if (bIsISM)
				{
					Info.ISMDescriptor = MoveTemp(TentativeISM);
				}
				else
				{
					Info.SMDescriptor = MoveTemp(TentativeSM);
				}
				// All contributors to this bucket share PropertyComponentHash by construction,
				// so the first actor's property data is canonical for the bucket.
				Info.RepresentativeActor = Actor;
			}

			const int32 VariantIdx = bCaptureMaterialOverrides
				? TrackMaterialVariant(SMC, Info)
				: -1;

			const FBox MeshBounds = Mesh->GetBoundingBox();

			if (bIsISM)
			{
				const int32 InstanceCount = ISMC->GetInstanceCount();
				if (InstanceCount == 0)
				{
					continue;
				}

				Info.TotalCount += InstanceCount;
				OutPoints.Reserve(OutPoints.Num() + InstanceCount);

				for (int32 Idx = 0; Idx < InstanceCount; Idx++)
				{
					FMeshPoint& Point = OutPoints.AddDefaulted_GetRef();
					Point.EntryKey = Key;
					Point.SourceComponent = ISMC;
					Point.SourceActor = Actor;
					Point.MaterialVariantIndex = VariantIdx;
					Point.BoundsMin = MeshBounds.Min;
					Point.BoundsMax = MeshBounds.Max;
					ISMC->GetInstanceTransform(Idx, Point.Transform, /*bWorldSpace=*/true);
				}
			}
			else
			{
				Info.TotalCount++;

				FMeshPoint& Point = OutPoints.AddDefaulted_GetRef();
				Point.EntryKey = Key;
				Point.SourceComponent = SMC;
				Point.SourceActor = Actor;
				Point.MaterialVariantIndex = VariantIdx;
				Point.Transform = SMC->GetComponentTransform();
				Point.BoundsMin = MeshBounds.Min;
				Point.BoundsMax = MeshBounds.Max;
			}
		}
	}

	// Registry that tracks which attribute name maps to which PCG metadata type.
	// First registration wins; subsequent conflicts are warned and discarded.
	struct FValueTagRegistry
	{
		TMap<FName, EPCGMetadataTypes> TypeMap;

		bool Register(const FName& Name, EPCGMetadataTypes NewType, const FString& SourceActorName)
		{
			if (const EPCGMetadataTypes* Existing = TypeMap.Find(Name))
			{
				if (*Existing != NewType)
				{
					UE_LOG(LogPCGEx, Warning,
					       TEXT("Value tag type conflict: '%s' on actor '%s'. Attribute was already registered with a different type; this actor's value will be discarded."),
					       *Name.ToString(), *SourceActorName);
					return false;
				}
				return true;
			}
			TypeMap.Add(Name, NewType);
			return true;
		}
	};

	// Per-actor parsed tag result.
	// PlainTags: tags with no colon (or unrecognized format) → become bool=true attributes.
	// ValueTags: Name:Value tags → become typed attributes.
	// Under ParseAndKeep, the name-part of each ValueTag is also written to the instance tag string
	// (handled at the call site by iterating both arrays).
	struct FParsedActorTags
	{
		TArray<FName> PlainTags;
		TArray<TPair<FName, TSharedPtr<PCGExData::IDataValue>>> ValueTags;
	};

	static FParsedActorTags ParseActorTags(const AActor* Actor, FValueTagRegistry& Registry)
	{
		FParsedActorTags Result;
		const FString ActorName = Actor->GetActorNameOrLabel();

		for (const FName& Tag : Actor->Tags)
		{
			FString Key;
			const TSharedPtr<PCGExData::IDataValue> DataValue = PCGExData::TryGetValueFromTag(Tag.ToString(), Key);

			if (DataValue.IsValid())
			{
				const FName AttrName(Key);
				if (Registry.Register(AttrName, DataValue->GetTypeId(), ActorName))
				{
					Result.ValueTags.Add(TPair<FName, TSharedPtr<PCGExData::IDataValue>>(AttrName, DataValue));
				}
				// Note: name-parts of value tags are intentionally NOT added to PlainTags.
				// They appear in the instance tag string only via the ValueTags array (ParseAndKeep).
			}
			else
			{
				// Plain tag → will become a bool=true attribute
				if (Registry.Register(Tag, EPCGMetadataTypes::Boolean, ActorName))
				{
					Result.PlainTags.Add(Tag);
				}
			}
		}
		return Result;
	}

	// Creates one typed PCG metadata attribute per registry entry; returns a map of base ptrs for fast per-point writes.
	static TMap<FName, FPCGMetadataAttributeBase*> CreateValueTagAttributes(UPCGMetadata* Meta, const FValueTagRegistry& Registry)
	{
		TMap<FName, FPCGMetadataAttributeBase*> AttrMap;
		AttrMap.Reserve(Registry.TypeMap.Num());

		for (const TPair<FName, EPCGMetadataTypes>& Elem : Registry.TypeMap)
		{
			const FName& Name = Elem.Key;
			FPCGMetadataAttributeBase* Attr = nullptr;
#define PCGEX_CREATE_VALUE_TAG_ATTR(_TYPE, _NAME) Attr = Meta->CreateAttribute<_TYPE>(Name, _TYPE{}, false, true);
			PCGEX_EXECUTEWITHRIGHTTYPE(Elem.Value, PCGEX_CREATE_VALUE_TAG_ATTR)
#undef PCGEX_CREATE_VALUE_TAG_ATTR
			if (Attr)
			{
				AttrMap.Add(Name, Attr);
			}
		}
		return AttrMap;
	}

	// Writes value-tag attributes for a single point.
	// PlainTags → bool=true; ValueTags → typed value via base-ptr cast.
	static void SetValueTagAttributes(
		const TMap<FName, FPCGMetadataAttributeBase*>& AttrMap,
		int64 Entry,
		const FParsedActorTags& Parsed)
	{
		for (const FName& Tag : Parsed.PlainTags)
		{
			if (FPCGMetadataAttributeBase* const* BasePtr = AttrMap.Find(Tag))
			{
				static_cast<FPCGMetadataAttribute<bool>*>(*BasePtr)->SetValue(Entry, true);
			}
		}

		for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
		{
			FPCGMetadataAttributeBase* const* BasePtr = AttrMap.Find(VT.Key);
			if (!BasePtr)
			{
				continue;
			}

			FPCGMetadataAttributeBase* Base = *BasePtr;
			const TSharedPtr<PCGExData::IDataValue>& Val = VT.Value;

#define PCGEX_SET_VALUE_TAG_ATTR(_TYPE, _NAME) static_cast<FPCGMetadataAttribute<_TYPE>*>(Base)->SetValue(Entry, Val->GetValue<_TYPE>());
			PCGEX_EXECUTEWITHRIGHTTYPE(Val->GetTypeId(), PCGEX_SET_VALUE_TAG_ATTR)
#undef PCGEX_SET_VALUE_TAG_ATTR
		}
	}

	// Creates value-tag attributes from Registry, then writes one row per parsed actor.
	// Parsed and Entries are parallel arrays; Entries[i] receives the attributes from Parsed[i].
	static void EmitValueTagAttributes(
		UPCGMetadata* Meta,
		const FValueTagRegistry& Registry,
		TConstArrayView<FParsedActorTags> Parsed,
		const TPCGValueRange<int64>& MetaEntries)
	{
		if (Parsed.IsEmpty() || Registry.TypeMap.IsEmpty())
		{
			return;
		}
		const TMap<FName, FPCGMetadataAttributeBase*> AttrMap = CreateValueTagAttributes(Meta, Registry);
		if (AttrMap.IsEmpty())
		{
			return;
		}
		for (int32 i = 0; i < Parsed.Num(); ++i)
		{
			SetValueTagAttributes(AttrMap, MetaEntries[i], Parsed[i]);
		}
	}
}

bool UPCGExDefaultLevelDataExporter::ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset)
{
	FPCGExLevelExportContext EmptyContext;
	return ExportLevelData(World, OutAsset, EmptyContext);
}

bool UPCGExDefaultLevelDataExporter::ExportLevelData(UWorld* World, UPCGDataAsset* OutAsset, FPCGExLevelExportContext& OutContext)
{
	if (!World || !OutAsset)
	{
		return false;
	}

	// The exporter never builds inline embedded mesh/level collections, never writes
	// Tag_EntryIdx, and never emplaces the CollectionMap pin. It captures mesh + level
	// contributions through OutContext (when pointers are non-null) and leaves final
	// compaction + hashing to the caller -- typically
	// UPCGExPCGDataAssetCollection::CompactSharedMesh / CompactSharedLevel /
	// RebuildCollectionMaps. The 2-arg BP-facing path delegates here with an empty
	// context; in that case the asset is produced without hashes (raw attributes only).

	ULevel* PersistentLevel = World->PersistentLevel;
	if (!PersistentLevel)
	{
		return false;
	}

	// Move any previous inner subobjects to the transient package so they get GC'd
	// instead of being saved as orphan exports in the collection's .uasset. Otherwise,
	// each rebuild accumulates dead sub-collections/point-data that can confuse save-time
	// pointer traversal (observed as INT_MAX-pointer crashes during level save).
	{
		TArray<UObject*> OldInners;
		GetObjectsWithOuter(OutAsset, OldInners, EGetObjectsFlags::None);
		for (UObject* Inner : OldInners)
		{
			Inner->Rename(nullptr, GetTransientPackage(),
			              REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}

	using namespace PCGExDefaultLevelDataExporterInternal;

	// Phase 1: Collect and classify qualifying actors
	TArray<FClassifiedActor> ClassifiedActors;
	for (AActor* Actor : PersistentLevel->Actors)
	{
		if (!UPCGExActorContentFilter::StaticPassesFilter(ContentFilter, Actor))
		{
			continue;
		}

		FClassifiedActor Classified;
		Classified.Actor = Actor;
		Classified.Type = ClassifyActor(Actor);

		if (Classified.Type != EPCGExActorExportType::Skip)
		{
			ClassifiedActors.Add(Classified);
		}
	}

	if (ClassifiedActors.IsEmpty())
	{
		return false;
	}

	// Separate by type
	TArray<FClassifiedActor> MeshActors;
	TArray<FClassifiedActor> ActorActors;
	TArray<FClassifiedActor> LevelActors;

	for (const FClassifiedActor& CA : ClassifiedActors)
	{
		if (CA.Type == EPCGExActorExportType::Mesh)
		{
			MeshActors.Add(CA);
		}
		else if (CA.Type == EPCGExActorExportType::Actor)
		{
			ActorActors.Add(CA);
		}
		else if (CA.Type == EPCGExActorExportType::Level)
		{
			LevelActors.Add(CA);
		}
	}

	// Group level instances by their referenced UWorld asset path.
	TMap<FSoftObjectPath, FLevelInfo> LevelInfoMap;
	for (const FClassifiedActor& CA : LevelActors)
	{
		const ALevelInstance* LI = Cast<ALevelInstance>(CA.Actor);
		if (!LI)
		{
			continue;
		}
		const FSoftObjectPath LevelPath = LI->GetWorldAsset().ToSoftObjectPath();
		if (LevelPath.IsNull())
		{
			continue;
		}
		LevelInfoMap.FindOrAdd(LevelPath).Count++;
	}

	// Parse actor value tags before the intersection loop so PlainTags/ValueTags are available.
	// ActorParsedTags is parallel to ActorActors (same index); empty when NoParsing.
	FValueTagRegistry ActorValueRegistry;
	TArray<FParsedActorTags> ActorParsedTags;

	if (ValueTagMode != EPCGExValueTagMode::NoParsing && !ActorActors.IsEmpty())
	{
		ActorParsedTags.Reserve(ActorActors.Num());
		for (const FClassifiedActor& CA : ActorActors)
		{
			ActorParsedTags.Add(ParseActorTags(CA.Actor, ActorValueRegistry));
		}
	}

	// Compute actor tag intersections and property deltas for collection building
	TMap<FActorInstanceKey, FActorClassInfo> ActorClassInfoMap;
	for (int32 i = 0; i < ActorActors.Num(); i++)
	{
		FClassifiedActor& CA = ActorActors[i];

		TArray<uint8> DeltaBytes;
		TArray<FSoftObjectPath> DeltaCollaterals;
		uint32 DeltaHash = 0;
		if (bCapturePropertyDeltas && bGenerateCollections)
		{
			DeltaBytes = PCGExActorDelta::SerializeActorDelta(CA.Actor, &DeltaCollaterals);
			DeltaHash = PCGExActorDelta::HashDelta(DeltaBytes);
		}
		CA.DeltaHash = DeltaHash;

		FActorInstanceKey Key;
		Key.ClassPath = FSoftClassPath(CA.Actor->GetClass());
		Key.DeltaHash = DeltaHash;

		FActorClassInfo& Info = ActorClassInfoMap.FindOrAdd(Key);
		Info.Count++;

		// Build the effective tag set for intersection based on the parsing mode.
		// Raw Actor->Tags are still used for property deltas (kept as-is).
		auto BuildEffectiveTags = [&]() -> TSet<FName>
		{
			TSet<FName> Tags;
			if (ActorParsedTags.IsValidIndex(i))
			{
				const FParsedActorTags& Parsed = ActorParsedTags[i];
				for (const FName& Tag : Parsed.PlainTags)
				{
					Tags.Add(Tag);
				}
				if (ValueTagMode == EPCGExValueTagMode::ParseAndKeep)
				{
					for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
					{
						Tags.Add(VT.Key);
					}
				}
			}
			else
			{
				for (const FName& Tag : CA.Actor->Tags)
				{
					Tags.Add(Tag);
				}
			}
			return Tags;
		};

		if (Info.bFirstActor)
		{
			Info.bFirstActor = false;
			if (!DeltaBytes.IsEmpty())
			{
				Info.SerializedDelta = MoveTemp(DeltaBytes);
				Info.CollateralPaths = MoveTemp(DeltaCollaterals);
			}
			Info.IntersectedTags = BuildEffectiveTags();
			Info.RepresentativeActor = CA.Actor;
		}
		else
		{
			Info.IntersectedTags = Info.IntersectedTags.Intersect(BuildEffectiveTags());
		}
	}

	// Phase 2: Create typed point data

	TArray<FMeshPoint> AllMeshPoints;
	TMap<FMeshEntryKey, FMeshInfo> MeshInfoMap;
	TMap<AActor*, FActorPropertySchemaCache> PropertySchemaCache;

	for (const FClassifiedActor& CA : MeshActors)
	{
		ExtractMeshPointsFromActor(CA.Actor, bCaptureMaterialOverrides, AllMeshPoints, MeshInfoMap, PropertySchemaCache);
	}

	// Create mesh point data
	UPCGBasePointData* MeshPointData = nullptr;
	if (!AllMeshPoints.IsEmpty())
	{
		// Parse value tags for each unique source actor (ISM actors may share the same actor).
		FValueTagRegistry MeshValueRegistry;
		TMap<AActor*, int32> MeshActorParsedTagIdx;
		TArray<FParsedActorTags> MeshActorParsedTagsList;

		if (ValueTagMode != EPCGExValueTagMode::NoParsing)
		{
			for (const FMeshPoint& Point : AllMeshPoints)
			{
				if (Point.SourceActor && !MeshActorParsedTagIdx.Contains(Point.SourceActor))
				{
					const int32 Idx = MeshActorParsedTagsList.Num();
					MeshActorParsedTagIdx.Add(Point.SourceActor, Idx);
					MeshActorParsedTagsList.Add(ParseActorTags(Point.SourceActor, MeshValueRegistry));
				}
			}
		}

		TPCGValueRange<FTransform> Transforms;
		TPCGValueRange<FVector> BMin, BMax;
		MeshPointData = CreatePointData(OutAsset, AllMeshPoints.Num(), Transforms, BMin, BMax);

		for (int32 i = 0; i < AllMeshPoints.Num(); i++)
		{
			const FMeshPoint& Point = AllMeshPoints[i];
			Transforms[i] = Point.Transform;
			BMin[i] = Point.BoundsMin;
			BMax[i] = Point.BoundsMax;
		}

		InitMetadata(MeshPointData, AllMeshPoints.Num());

		UPCGMetadata* Meta = MeshPointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = MeshPointData->GetMetadataEntryValueRange();

		TMap<FName, FPCGMetadataAttributeBase*> MeshValueTagAttrMap;
		if (!MeshActorParsedTagsList.IsEmpty())
		{
			MeshValueTagAttrMap = CreateValueTagAttributes(Meta, MeshValueRegistry);
		}

		FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);

		if (!bGenerateCollections)
		{
			FPCGMetadataAttribute<FSoftObjectPath>* MeshAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, true);

			for (int32 i = 0; i < AllMeshPoints.Num(); i++)
			{
				const int64 Entry = MetaEntries[i];
				if (ActorNameAttr)
				{
					ActorNameAttr->SetValue(Entry, AllMeshPoints[i].SourceActor->GetActorNameOrLabel());
				}
				if (MeshAttr)
				{
					MeshAttr->SetValue(Entry, AllMeshPoints[i].EntryKey.MeshPath);
				}
			}
		}
		else
		{
			for (int32 i = 0; i < AllMeshPoints.Num(); i++)
			{
				if (ActorNameAttr)
				{
					ActorNameAttr->SetValue(MetaEntries[i], AllMeshPoints[i].SourceActor->GetActorNameOrLabel());
				}
			}
		}

		if (!MeshValueTagAttrMap.IsEmpty())
		{
			for (int32 i = 0; i < AllMeshPoints.Num(); i++)
			{
				if (const int32* PIdx = MeshActorParsedTagIdx.Find(AllMeshPoints[i].SourceActor))
				{
					SetValueTagAttributes(MeshValueTagAttrMap, MetaEntries[i], MeshActorParsedTagsList[*PIdx]);
				}
			}
		}

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = MeshPointData;
		TaggedData.Pin = PCGExCollections::Labels::MeshesPin;
	}

	// --- Actors ---
	UPCGBasePointData* ActorPointData = nullptr;
	if (!ActorActors.IsEmpty())
	{
		TPCGValueRange<FTransform> Transforms;
		TPCGValueRange<FVector> BMin, BMax;
		ActorPointData = CreatePointData(OutAsset, ActorActors.Num(), Transforms, BMin, BMax);

		for (int32 i = 0; i < ActorActors.Num(); i++)
		{
			WriteActorTransformAndBounds(ActorActors[i].Actor, i, BoundsEvaluator, Transforms, BMin, BMax);
		}

		InitMetadata(ActorPointData, ActorActors.Num());

		UPCGMetadata* Meta = ActorPointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = ActorPointData->GetMetadataEntryValueRange();

		FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);

		if (!bGenerateCollections)
		{
			FPCGMetadataAttribute<FSoftClassPath>* ActorClassAttr = Meta->CreateAttribute<FSoftClassPath>(TEXT("ActorClass"), FSoftClassPath(), false, true);

			for (int32 i = 0; i < ActorActors.Num(); i++)
			{
				const int64 Entry = MetaEntries[i];
				if (ActorNameAttr)
				{
					ActorNameAttr->SetValue(Entry, ActorActors[i].Actor->GetActorNameOrLabel());
				}
				if (ActorClassAttr)
				{
					ActorClassAttr->SetValue(Entry, FSoftClassPath(ActorActors[i].Actor->GetClass()));
				}
			}
		}
		else
		{
			for (int32 i = 0; i < ActorActors.Num(); i++)
			{
				if (ActorNameAttr)
				{
					ActorNameAttr->SetValue(MetaEntries[i], ActorActors[i].Actor->GetActorNameOrLabel());
				}
			}
		}

		EmitValueTagAttributes(Meta, ActorValueRegistry, ActorParsedTags, MetaEntries);

		// Write per-actor tag names as a joined string. Only meaningful in NoParsing and ParseAndKeep
		// modes; in Parse mode all tags are already written as typed attributes above.
		if (bWriteInstanceTags && InstanceTagsAttributeName != NAME_None && ValueTagMode != EPCGExValueTagMode::Parse)
		{
			if (FPCGMetadataAttribute<FString>* InstanceTagsAttr = Meta->CreateAttribute<FString>(InstanceTagsAttributeName, FString(), false, true))
			{
				for (int32 i = 0; i < ActorActors.Num(); i++)
				{
					FString TagsStr;

					if (ValueTagMode == EPCGExValueTagMode::NoParsing)
					{
						for (const FName& Tag : ActorActors[i].Actor->Tags)
						{
							if (!TagsStr.IsEmpty())
							{
								TagsStr += TEXT(",");
							}
							TagsStr += Tag.ToString();
						}
					}
					else if (ActorParsedTags.IsValidIndex(i)) // ParseAndKeep: plain tag names + value-tag name-parts
					{
						const FParsedActorTags& Parsed = ActorParsedTags[i];
						for (const FName& Tag : Parsed.PlainTags)
						{
							if (!TagsStr.IsEmpty())
							{
								TagsStr += TEXT(",");
							}
							TagsStr += Tag.ToString();
						}
						for (const TPair<FName, TSharedPtr<PCGExData::IDataValue>>& VT : Parsed.ValueTags)
						{
							if (!TagsStr.IsEmpty())
							{
								TagsStr += TEXT(",");
							}
							TagsStr += VT.Key.ToString();
						}
					}

					if (!TagsStr.IsEmpty())
					{
						InstanceTagsAttr->SetValue(MetaEntries[i], TagsStr);
					}
				}
			}
		}

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = ActorPointData;
		TaggedData.Pin = PCGExCollections::Labels::ActorsPin;
	}

	// --- Levels (nested level instances) ---
	UPCGBasePointData* LevelPointData = nullptr;
	if (!LevelActors.IsEmpty())
	{
		FValueTagRegistry LevelValueRegistry;
		TArray<FParsedActorTags> LevelParsedTags;

		if (ValueTagMode != EPCGExValueTagMode::NoParsing)
		{
			LevelParsedTags.Reserve(LevelActors.Num());
			for (const FClassifiedActor& CA : LevelActors)
			{
				LevelParsedTags.Add(ParseActorTags(CA.Actor, LevelValueRegistry));
			}
		}

		TPCGValueRange<FTransform> Transforms;
		TPCGValueRange<FVector> BMin, BMax;
		LevelPointData = CreatePointData(OutAsset, LevelActors.Num(), Transforms, BMin, BMax);

		for (int32 i = 0; i < LevelActors.Num(); i++)
		{
			WriteActorTransformAndBounds(LevelActors[i].Actor, i, BoundsEvaluator, Transforms, BMin, BMax);
		}

		InitMetadata(LevelPointData, LevelActors.Num());

		UPCGMetadata* Meta = LevelPointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = LevelPointData->GetMetadataEntryValueRange();

		FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);

		if (!bGenerateCollections)
		{
			FPCGMetadataAttribute<FSoftObjectPath>* LevelAssetAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("LevelAsset"), FSoftObjectPath(), false, true);

			for (int32 i = 0; i < LevelActors.Num(); i++)
			{
				const int64 Entry = MetaEntries[i];
				if (ActorNameAttr)
				{
					ActorNameAttr->SetValue(Entry, LevelActors[i].Actor->GetActorNameOrLabel());
				}
				if (LevelAssetAttr)
				{
					if (const ALevelInstance* LI = Cast<ALevelInstance>(LevelActors[i].Actor))
					{
						LevelAssetAttr->SetValue(Entry, LI->GetWorldAsset().ToSoftObjectPath());
					}
				}
			}
		}
		else
		{
			for (int32 i = 0; i < LevelActors.Num(); i++)
			{
				if (ActorNameAttr)
				{
					ActorNameAttr->SetValue(MetaEntries[i], LevelActors[i].Actor->GetActorNameOrLabel());
				}
			}
		}

		EmitValueTagAttributes(Meta, LevelValueRegistry, LevelParsedTags, MetaEntries);

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = LevelPointData;
		TaggedData.Pin = PCGExCollections::Labels::LevelsPin;
	}

	// Phase 2.5: Notify subclasses
	OnExportComplete(OutAsset);

	// Phase 3: Collection-flavored capture (when bGenerateCollections).
	// Builds the in-memory mesh + level entry lists and the per-entry actor collection,
	// then assigns local-pick indices to each point. No inline shared collections are
	// built, no Tag_EntryIdx attribute is written, and no CollectionMap pin is emplaced
	// here -- those are the caller's responsibility (see UPCGExPCGDataAssetCollection
	// shared-collection API). The per-entry actor collection IS built here because it
	// has no cross-entry mutualization story.
	if (bGenerateCollections)
	{
		// Build the mesh entry list. MeshInfoMap.EntryIndex is assigned here so the local-pick
		// write below can map (MeshPath → local entry index) consistently.
		TArray<FPCGExMeshCollectionEntry> MeshEntries;
		if (!MeshInfoMap.IsEmpty())
		{
			MeshEntries.SetNum(MeshInfoMap.Num());
			int32 MeshIdx = 0;
			for (auto& Elem : MeshInfoMap)
			{
				Elem.Value.EntryIndex = MeshIdx;

				FPCGExMeshCollectionEntry& MeshEntry = MeshEntries[MeshIdx];
				MeshEntry.StaticMesh = TSoftObjectPtr<UStaticMesh>(Elem.Key.MeshPath);
				MeshEntry.Weight = Elem.Value.TotalCount;
				// Carry the per-bucket property-component identity onto the entry so
				// CompactSharedMesh dedup honors it.
				MeshEntry.PropertyComponentHash = Elem.Key.PropertyComponentHash;

				if (Elem.Key.bIsISMSource)
				{
					MeshEntry.ISMDescriptor = Elem.Value.ISMDescriptor;
				}
				else
				{
					MeshEntry.SMDescriptor = Elem.Value.SMDescriptor;
				}

				// Material variants
				if (bCaptureMaterialOverrides && Elem.Value.UniqueVariantMaterials.Num() > 1)
				{
					MeshEntry.MaterialVariants = EPCGExMaterialVariantsMode::Multi;

					for (const TArray<FSoftObjectPath>& VariantMats : Elem.Value.UniqueVariantMaterials)
					{
						FPCGExMaterialOverrideCollection& Variant = MeshEntry.MaterialOverrideVariantsList.AddDefaulted_GetRef();
						Variant.Weight = 1;

						for (int32 SlotIdx = 0; SlotIdx < VariantMats.Num(); SlotIdx++)
						{
							FPCGExMaterialOverrideEntry& MatEntry = Variant.Overrides.AddDefaulted_GetRef();
							MatEntry.SlotIndex = SlotIdx;
							MatEntry.Material = TSoftObjectPtr<UMaterialInterface>(VariantMats[SlotIdx]);
						}
					}
				}

				// Pull property-component values from the representative actor and stage them
				// onto the mesh entry's PropertyOverrides. CompactSharedMesh preserves these
				// across cross-entry aggregation; the SharedMeshCollection's CollectionProperties
				// is rebuilt from the union of all entries' overrides afterwards via
				// RefreshCollectionPropertiesFromEntries.
				//
				// Read from the schema cache populated during the per-actor mesh-extract loop --
				// the representative actor was hashed there, so its Resolved schema is already
				// computed. Fall back to a fresh extract for the (rare) bucket without one (shouldn't
				// happen for property-bearing actors, but the cache is non-authoritative).
				const TArray<FInstancedStruct>* Authored = nullptr;
				TArray<FInstancedStruct> FallbackAuthored;
				if (const FActorPropertySchemaCache* Cached = PropertySchemaCache.Find(Elem.Value.RepresentativeActor.Get()))
				{
					Authored = &Cached->Resolved;
				}
				else
				{
					FallbackAuthored = UPCGExPropertyCollectionComponent::ExtractSchemaFromActor(
						Elem.Value.RepresentativeActor.Get());
					Authored = &FallbackAuthored;
				}

				if (!Authored->IsEmpty())
				{
					MeshEntry.PropertyOverrides.Overrides.Reset(Authored->Num());
					for (const FInstancedStruct& Prop : *Authored)
					{
						// Seed outer identity from inner so the entry ships in canonical
						// SyncToSchema shape; first reconcile would otherwise migrate it.
						FPCGExPropertyOverrideEntry& Slot = MeshEntry.PropertyOverrides.Overrides.AddDefaulted_GetRef();
						Slot.Value = Prop;
						Slot.bEnabled = true;
#if WITH_EDITORONLY_DATA
						Slot.SeedOuterIdentityFromInner();
#endif
					}
				}

				MeshIdx++;
			}
		}

		// Build the level entry list. LevelInfoMap.EntryIndex is assigned here so the
		// local-pick write below can map (LevelPath → local entry index) consistently.
		TArray<FPCGExLevelCollectionEntry> LevelEntries;
		if (!LevelInfoMap.IsEmpty())
		{
			LevelEntries.SetNum(LevelInfoMap.Num());
			int32 LevelIdx = 0;
			for (auto& Elem : LevelInfoMap)
			{
				Elem.Value.EntryIndex = LevelIdx;

				FPCGExLevelCollectionEntry& LevelEntry = LevelEntries[LevelIdx];
				LevelEntry.Level = TSoftObjectPtr<UWorld>(Elem.Key);
				LevelEntry.Weight = Elem.Value.Count;

				LevelIdx++;
			}
		}

		// Build the per-entry actor collection (no cross-entry mutualization).
		UPCGExActorCollection* EmbeddedActorCollection = nullptr;

		if (!ActorClassInfoMap.IsEmpty())
		{
			// EntryIds re-claimed by actor identity: exact = class + property-delta hash,
			// loose = class alone (survives a delta change). Unclaimed entries get fresh ids
			// from the RebuildStagingData -> SyncEntryIds pass.
			PCGExAssetCollection::FEntryIdBank PreservedIds;

			// Reuse the previous collection UObject -- a fresh one would mint a new
			// CollectionGUID on every export, unbinding external references (variants).
			if (UPCGExActorCollection* Previous = OutContext.PreviousActorCollection)
			{
				for (const FPCGExActorCollectionEntry& PrevEntry : Previous->Entries)
				{
					// Rebuilt as an FActorInstanceKey so deposit and claim share one key derivation.
					FActorInstanceKey PrevKey;
					PrevKey.ClassPath = FSoftClassPath(PrevEntry.Actor.ToString());
					PrevKey.DeltaHash = PCGExActorDelta::HashDelta(PrevEntry.SerializedPropertyDelta);
					PreservedIds.Deposit(GetTypeHash(PrevKey), GetTypeHash(PrevKey.ClassPath), PrevEntry.EntryId);
				}

				Previous->Rename(nullptr, OutAsset, REN_DontCreateRedirectors | REN_NonTransactional);
				// InitNumEntries SetNums without clearing -- shrink would leave stale survivors.
				Previous->Entries.Reset();
				EmbeddedActorCollection = Previous;
			}
			else
			{
				EmbeddedActorCollection = NewObject<UPCGExActorCollection>(OutAsset);
			}

			EmbeddedActorCollection->InitNumEntries(ActorClassInfoMap.Num());

			// Parallel to Entries: one live donor per entry, used by the property-component scan
			// below. The bucket's RepresentativeActor was captured at the same point we captured
			// SerializedDelta, so the scan reads from the actor whose authored state the delta
			// already reflects.
			TArray<AActor*> RepresentativeInstances;
			RepresentativeInstances.Init(nullptr, ActorClassInfoMap.Num());

			int32 ActorIdx = 0;
			for (auto& Elem : ActorClassInfoMap)
			{
				Elem.Value.EntryIndex = ActorIdx;

				FPCGExActorCollectionEntry& ActorEntry = EmbeddedActorCollection->Entries[ActorIdx];
				ActorEntry.Actor = TSoftClassPtr<AActor>(Elem.Key.ClassPath);
				ActorEntry.Weight = Elem.Value.Count;
				ActorEntry.Tags = Elem.Value.IntersectedTags;

				if (!Elem.Value.SerializedDelta.IsEmpty())
				{
					ActorEntry.SerializedPropertyDelta = Elem.Value.SerializedDelta;
					ActorEntry.DeltaCollateralPaths = Elem.Value.CollateralPaths;
				}

				ActorEntry.EntryId = PreservedIds.ClaimExact(GetTypeHash(Elem.Key));

				RepresentativeInstances[ActorIdx] = Elem.Value.RepresentativeActor.Get();

				ActorIdx++;
			}

			// Loose pass -- strictly after every exact claim; class path mirrors the deposit's loose key.
			for (FPCGExActorCollectionEntry& ActorEntry : EmbeddedActorCollection->Entries)
			{
				if (ActorEntry.EntryId == 0)
				{
					ActorEntry.EntryId = PreservedIds.ClaimLoose(GetTypeHash(ActorEntry.Actor.ToSoftObjectPath()));
				}
			}

			// Scan UPCGExPropertyCollectionComponent on each representative actor and merge into
			// the embedded collection's schema + per-entry overrides. Runs BEFORE RebuildStagingData
			// so the staging pass observes the final schema; the non-editor RebuildStagingData
			// variant does not fire EDITOR_OnPostStagingRebuild, so there is no double-scan from
			// the UPCGExActorCollection auto-hook on this code path.
			//
			// The exporter's policy is also mirrored onto the embedded collection's own field so
			// a user-triggered manual rebuild of the embedded asset uses the same policy that
			// generated it.
			EmbeddedActorCollection->SchemaMergePolicy = SchemaMergePolicy;
			EmbeddedActorCollection->RebuildPropertiesFromActorComponents(
				SchemaMergePolicy, RepresentativeInstances);

			EmbeddedActorCollection->RebuildStagingData(true);
		}

		if (OutContext.ActorCollectionOut)
		{
			*OutContext.ActorCollectionOut = EmbeddedActorCollection;
		}

		// Capture per-mesh-point local picks (low 16 = local entry index, high 16 = sec+1).
		// Tag_EntryIdx is left unwritten; the caller resolves shared indices and writes
		// the final hashes during shared-collection compaction.
		if (MeshPointData && !MeshEntries.IsEmpty() && OutContext.MeshLocalPicks)
		{
			TArray<int32>& LocalPicksOut = *OutContext.MeshLocalPicks;
			LocalPicksOut.SetNumUninitialized(AllMeshPoints.Num());

			for (int32 i = 0; i < AllMeshPoints.Num(); i++)
			{
				const FMeshPoint& Point = AllMeshPoints[i];
				const FMeshInfo* Info = MeshInfoMap.Find(Point.EntryKey);
				if (!Info)
				{
					// Sentinel: -1 means "no pick" -- rewrite pass leaves the hash unwritten.
					LocalPicksOut[i] = -1;
					continue;
				}

				const int16 SecIdx = (bCaptureMaterialOverrides && Point.MaterialVariantIndex > 0)
					? static_cast<int16>(Point.MaterialVariantIndex)
					: static_cast<int16>(-1);

				LocalPicksOut[i] = FPCGExLevelExportContext::PackLocalPick(Info->EntryIndex, SecIdx);
			}
		}

		// Encode actor hashes inline -- actor collection is per-entry, so the hash is resolved
		// here against EmbeddedActorCollection's own GUID. The caller does not rewrite actor
		// hashes (the CollectionMap rebuild simply re-registers the same actor collection).
		if (ActorPointData && EmbeddedActorCollection)
		{
			PCGExCollections::FPickPacker ActorPacker;
			ActorPacker.RegisterCollection(EmbeddedActorCollection);

			UPCGMetadata* Meta = ActorPointData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = ActorPointData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, 0, false, true);

			if (EntryHashAttr)
			{
				for (int32 i = 0; i < ActorActors.Num(); i++)
				{
					FActorInstanceKey Key;
					Key.ClassPath = FSoftClassPath(ActorActors[i].Actor->GetClass());
					Key.DeltaHash = ActorActors[i].DeltaHash;
					const FActorClassInfo* Info = ActorClassInfoMap.Find(Key);
					if (!Info)
					{
						continue;
					}

					const uint64 Hash = ActorPacker.GetPickIdx(EmbeddedActorCollection, static_cast<int16>(Info->EntryIndex), -1);
					EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash));
				}
			}
		}

		// Capture per-level-point local picks (identity of local entry index, -1 sentinel).
		if (LevelPointData && !LevelEntries.IsEmpty() && OutContext.LevelLocalPicks)
		{
			TArray<int32>& LocalPicksOut = *OutContext.LevelLocalPicks;
			LocalPicksOut.SetNumUninitialized(LevelActors.Num());

			for (int32 i = 0; i < LevelActors.Num(); i++)
			{
				const ALevelInstance* LI = Cast<ALevelInstance>(LevelActors[i].Actor);
				if (!LI)
				{
					LocalPicksOut[i] = -1;
					continue;
				}
				const FSoftObjectPath LevelPath = LI->GetWorldAsset().ToSoftObjectPath();
				const FLevelInfo* Info = LevelInfoMap.Find(LevelPath);
				if (!Info)
				{
					LocalPicksOut[i] = -1;
					continue;
				}

				LocalPicksOut[i] = Info->EntryIndex;
			}
		}

		// Compute the "common-ancestor" inherited-defaults view for the contributing actors:
		// per property, the value all unique BP classes agree on at the CDO level (the
		// BuildInheritedSchema chain walk WITHOUT instance overrides); when classes disagree,
		// fall back to the asset's authored default. Consumed by CompactSharedMesh to seed the
		// shared MeshCollection's CollectionProperties.
		if (OutContext.MeshInheritedDefaults)
		{
			TMap<UClass*, TArray<FInstancedStruct>> InheritedByClass;
			TMap<UClass*, TArray<FInstancedStruct>> AssetDefaultsByClass;
			for (const TPair<AActor*, FActorPropertySchemaCache>& Pair : PropertySchemaCache)
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
			*OutContext.MeshInheritedDefaults = PCGExProperties::AggregateAgreedValuesByName(InheritedViews, AssetDefaultViews);
		}

		// Hand the captured mesh + level entry lists back to the caller for compaction.
		if (OutContext.MeshContributions)
		{
			*OutContext.MeshContributions = MoveTemp(MeshEntries);
		}
		if (OutContext.LevelContributions)
		{
			*OutContext.LevelContributions = MoveTemp(LevelEntries);
		}
	}

	return OutAsset->Data.TaggedData.Num() > 0;
}
