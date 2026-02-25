// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExDefaultLevelDataExporter.h"

#include "PCGDataAsset.h"
#include "PCGParamData.h"
#include "Data/PCGPointArrayData.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"
#include "Helpers/PCGExCollectionsHelpers.h"

#include "Collections/PCGExMeshCollection.h"
#include "Collections/PCGExActorCollection.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Engine/LevelScriptActor.h"
#include "Engine/Brush.h"
#include "GameFramework/Info.h"
#endif

#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

#include "Helpers/PCGExActorPropertyDelta.h"

EPCGExActorExportType UPCGExDefaultLevelDataExporter::ClassifyActor(AActor* Actor, UStaticMeshComponent*& OutMeshComponent) const
{
	OutMeshComponent = Actor->FindComponentByClass<UStaticMeshComponent>();
	if (OutMeshComponent && OutMeshComponent->GetStaticMesh())
	{
		return EPCGExActorExportType::Mesh;
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
		UStaticMeshComponent* MeshComponent = nullptr;
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
			DelayedEntries[i] = MakeTuple(MetaEntries[i], int64(-1));
		}

		Meta->AddDelayedEntries(DelayedEntries);
	}

	static void WriteActorTransformAndBounds(
		AActor* Actor, int32 Index,
		TPCGValueRange<FTransform>& Transforms,
		TPCGValueRange<FVector>& BoundsMin,
		TPCGValueRange<FVector>& BoundsMax)
	{
		const FTransform ActorTransform = Actor->GetActorTransform();
		Transforms[Index] = ActorTransform;

		FVector Origin, BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent);

		const FTransform InvTransform = ActorTransform.Inverse();
		const FVector LocalCenter = InvTransform.TransformPosition(Origin);

		BoundsMin[Index] = LocalCenter - BoxExtent;
		BoundsMax[Index] = LocalCenter + BoxExtent;
	}

	struct FMeshPoint
	{
		FTransform Transform;
		FVector BoundsMin = FVector::ZeroVector;
		FVector BoundsMax = FVector::ZeroVector;
		FSoftObjectPath MeshPath;
		const UStaticMeshComponent* SourceComponent = nullptr;
		AActor* SourceActor = nullptr;
		int32 MaterialVariantIndex = -1;
	};

	struct FMeshInfo
	{
		int32 EntryIndex = -1;
		const UStaticMeshComponent* FirstComponent = nullptr;
		int32 TotalCount = 0;
		TArray<TArray<FSoftObjectPath>> UniqueVariantMaterials;
		TMap<uint32, int32> VariantHashToIndex;
	};

	struct FActorClassInfo
	{
		int32 EntryIndex = -1;
		TSet<FName> IntersectedTags;
		bool bFirstActor = true;
		int32 Count = 0;
		TArray<uint8> SerializedDelta;
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

	static int32 TrackMaterialVariant(const UStaticMeshComponent* Comp, FMeshInfo& Info)
	{
		if (!Comp) { return -1; }

		const uint32 MatHash = HashMaterials(Comp);
		if (MatHash == 0) { return -1; }

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

}

bool UPCGExDefaultLevelDataExporter::ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset)
{
	if (!World || !OutAsset) { return false; }

	ULevel* PersistentLevel = World->PersistentLevel;
	if (!PersistentLevel) { return false; }

	using namespace PCGExDefaultLevelDataExporterInternal;

	// Phase 1: Collect and classify qualifying actors
	TArray<FClassifiedActor> ClassifiedActors;
	for (AActor* Actor : PersistentLevel->Actors)
	{
		if (!Actor) { continue; }
		if (Actor->IsHidden()) { continue; }
		if (Actor->bIsEditorOnlyActor) { continue; }

#if WITH_EDITOR
		if (Actor->IsA<ALevelScriptActor>()) { continue; }
		if (Actor->IsA<AInfo>()) { continue; }
		if (Actor->IsA<ABrush>()) { continue; }
#endif

		// Tag include filter
		if (IncludeTags.Num() > 0)
		{
			bool bHasIncludeTag = false;
			for (const FName& Tag : IncludeTags)
			{
				if (Actor->Tags.Contains(Tag)) { bHasIncludeTag = true; break; }
			}
			if (!bHasIncludeTag) { continue; }
		}

		// Tag exclude filter
		if (ExcludeTags.Num() > 0)
		{
			bool bHasExcludeTag = false;
			for (const FName& Tag : ExcludeTags)
			{
				if (Actor->Tags.Contains(Tag)) { bHasExcludeTag = true; break; }
			}
			if (bHasExcludeTag) { continue; }
		}

		// Class include filter
		if (IncludeClasses.Num() > 0)
		{
			bool bMatchesClass = false;
			for (const TSoftClassPtr<AActor>& ClassPtr : IncludeClasses)
			{
				if (UClass* C = ClassPtr.Get()) { if (Actor->IsA(C)) { bMatchesClass = true; break; } }
			}
			if (!bMatchesClass) { continue; }
		}

		// Class exclude filter
		if (ExcludeClasses.Num() > 0)
		{
			bool bExcluded = false;
			for (const TSoftClassPtr<AActor>& ClassPtr : ExcludeClasses)
			{
				if (UClass* C = ClassPtr.Get()) { if (Actor->IsA(C)) { bExcluded = true; break; } }
			}
			if (bExcluded) { continue; }
		}

		FClassifiedActor Classified;
		Classified.Actor = Actor;
		Classified.Type = ClassifyActor(Actor, Classified.MeshComponent);

		if (Classified.Type != EPCGExActorExportType::Skip)
		{
			ClassifiedActors.Add(Classified);
		}
	}

	if (ClassifiedActors.IsEmpty()) { return false; }

	// Separate by type
	TArray<FClassifiedActor> MeshActors;
	TArray<FClassifiedActor> ActorActors;

	for (const FClassifiedActor& CA : ClassifiedActors)
	{
		if (CA.Type == EPCGExActorExportType::Mesh) { MeshActors.Add(CA); }
		else if (CA.Type == EPCGExActorExportType::Actor) { ActorActors.Add(CA); }
	}

	// Compute actor tag intersections and property deltas for collection building
	TMap<FActorInstanceKey, FActorClassInfo> ActorClassInfoMap;
	for (FClassifiedActor& CA : ActorActors)
	{
		TArray<uint8> DeltaBytes;
		uint32 DeltaHash = 0;
		if (bCapturePropertyDeltas && bGenerateCollections)
		{
			DeltaBytes = PCGExActorDelta::SerializeActorDelta(CA.Actor);
			DeltaHash = PCGExActorDelta::HashDelta(DeltaBytes);
		}
		CA.DeltaHash = DeltaHash;

		FActorInstanceKey Key;
		Key.ClassPath = FSoftClassPath(CA.Actor->GetClass());
		Key.DeltaHash = DeltaHash;

		FActorClassInfo& Info = ActorClassInfoMap.FindOrAdd(Key);
		Info.Count++;

		if (Info.bFirstActor)
		{
			Info.bFirstActor = false;
			if (!DeltaBytes.IsEmpty()) { Info.SerializedDelta = MoveTemp(DeltaBytes); }
			for (const FName& Tag : CA.Actor->Tags) { Info.IntersectedTags.Add(Tag); }
		}
		else
		{
			TSet<FName> ActorTags;
			for (const FName& Tag : CA.Actor->Tags) { ActorTags.Add(Tag); }
			Info.IntersectedTags = Info.IntersectedTags.Intersect(ActorTags);
		}
	}

	// Phase 2: Create typed point data

	// --- Unified Meshes (SM actors + ISM instances) ---
	TArray<FMeshPoint> AllMeshPoints;
	TMap<FSoftObjectPath, FMeshInfo> MeshInfoMap;

	// Collect from SM actors
	for (const FClassifiedActor& CA : MeshActors)
	{
		if (!CA.MeshComponent) { continue; }
		UStaticMesh* Mesh = CA.MeshComponent->GetStaticMesh();
		if (!Mesh) { continue; }

		const FSoftObjectPath MeshPath(Mesh);
		FMeshInfo& Info = MeshInfoMap.FindOrAdd(MeshPath);
		Info.TotalCount++;
		if (!Info.FirstComponent) { Info.FirstComponent = CA.MeshComponent; }

		FMeshPoint& Point = AllMeshPoints.AddDefaulted_GetRef();
		Point.MeshPath = MeshPath;
		Point.SourceComponent = CA.MeshComponent;
		Point.SourceActor = CA.Actor;

		const FTransform ActorTransform = CA.Actor->GetActorTransform();
		Point.Transform = ActorTransform;

		FVector Origin, BoxExtent;
		CA.Actor->GetActorBounds(false, Origin, BoxExtent);
		const FTransform InvTransform = ActorTransform.Inverse();
		const FVector LocalCenter = InvTransform.TransformPosition(Origin);
		Point.BoundsMin = LocalCenter - BoxExtent;
		Point.BoundsMax = LocalCenter + BoxExtent;

		if (bCaptureMaterialOverrides)
		{
			Point.MaterialVariantIndex = TrackMaterialVariant(CA.MeshComponent, Info);
		}
	}

	// Collect from ISM instances on all classified actors
	for (const FClassifiedActor& CA : ClassifiedActors)
	{
		TArray<UInstancedStaticMeshComponent*> ISMComponents;
		CA.Actor->GetComponents<UInstancedStaticMeshComponent>(ISMComponents);

		for (const UInstancedStaticMeshComponent* ISM : ISMComponents)
		{
			if (!ISM || ISM->GetInstanceCount() == 0) { continue; }

			UStaticMesh* Mesh = ISM->GetStaticMesh();
			if (!Mesh) { continue; }

			const FSoftObjectPath MeshPath(Mesh);
			const int32 InstanceCount = ISM->GetInstanceCount();

			FMeshInfo& Info = MeshInfoMap.FindOrAdd(MeshPath);
			Info.TotalCount += InstanceCount;
			if (!Info.FirstComponent) { Info.FirstComponent = ISM; }

			const FBox MeshBounds = Mesh->GetBoundingBox();

			int32 VariantIdx = -1;
			if (bCaptureMaterialOverrides)
			{
				VariantIdx = TrackMaterialVariant(ISM, Info);
			}

			for (int32 Idx = 0; Idx < InstanceCount; Idx++)
			{
				FMeshPoint& Point = AllMeshPoints.AddDefaulted_GetRef();
				Point.MeshPath = MeshPath;
				Point.SourceComponent = ISM;
				Point.SourceActor = CA.Actor;
				Point.MaterialVariantIndex = VariantIdx;

				ISM->GetInstanceTransform(Idx, Point.Transform, true);
				Point.BoundsMin = MeshBounds.Min;
				Point.BoundsMax = MeshBounds.Max;
			}
		}
	}

	// Create mesh point data
	UPCGBasePointData* MeshPointData = nullptr;
	if (!AllMeshPoints.IsEmpty())
	{
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

		FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);

		if (!bGenerateCollections)
		{
			FPCGMetadataAttribute<FSoftObjectPath>* MeshAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, true);

			for (int32 i = 0; i < AllMeshPoints.Num(); i++)
			{
				const int64 Entry = MetaEntries[i];
				if (ActorNameAttr) { ActorNameAttr->SetValue(Entry, AllMeshPoints[i].SourceActor->GetActorNameOrLabel()); }
				if (MeshAttr) { MeshAttr->SetValue(Entry, AllMeshPoints[i].MeshPath); }
			}
		}
		else
		{
			for (int32 i = 0; i < AllMeshPoints.Num(); i++)
			{
				if (ActorNameAttr) { ActorNameAttr->SetValue(MetaEntries[i], AllMeshPoints[i].SourceActor->GetActorNameOrLabel()); }
			}
		}

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = MeshPointData;
		TaggedData.Pin = FName(TEXT("Meshes"));
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
			WriteActorTransformAndBounds(ActorActors[i].Actor, i, Transforms, BMin, BMax);
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
				if (ActorNameAttr) { ActorNameAttr->SetValue(Entry, ActorActors[i].Actor->GetActorNameOrLabel()); }
				if (ActorClassAttr) { ActorClassAttr->SetValue(Entry, FSoftClassPath(ActorActors[i].Actor->GetClass())); }
			}
		}
		else
		{
			for (int32 i = 0; i < ActorActors.Num(); i++)
			{
				if (ActorNameAttr) { ActorNameAttr->SetValue(MetaEntries[i], ActorActors[i].Actor->GetActorNameOrLabel()); }
			}
		}

		// Write per-point instance tags delta
		FPCGMetadataAttribute<FString>* InstanceTagsAttr = Meta->CreateAttribute<FString>(TEXT("InstanceTags"), FString(), false, true);
		if (InstanceTagsAttr)
		{
			for (int32 i = 0; i < ActorActors.Num(); i++)
			{
				FActorInstanceKey Key;
				Key.ClassPath = FSoftClassPath(ActorActors[i].Actor->GetClass());
				Key.DeltaHash = ActorActors[i].DeltaHash;
				const FActorClassInfo* Info = ActorClassInfoMap.Find(Key);
				if (!Info) { continue; }

				// Compute delta: actor tags minus intersection
				FString DeltaStr;
				for (const FName& Tag : ActorActors[i].Actor->Tags)
				{
					if (!Info->IntersectedTags.Contains(Tag))
					{
						if (!DeltaStr.IsEmpty()) { DeltaStr += TEXT(","); }
						DeltaStr += Tag.ToString();
					}
				}

				if (!DeltaStr.IsEmpty())
				{
					InstanceTagsAttr->SetValue(MetaEntries[i], DeltaStr);
				}
			}
		}

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = ActorPointData;
		TaggedData.Pin = FName(TEXT("Actors"));
	}

	// Phase 2.5: Notify subclasses
	OnExportComplete(OutAsset);

	// Phase 3: Embedded collection generation (when bGenerateCollections)
	if (bGenerateCollections)
	{
		// Build embedded mesh collection
		UPCGExMeshCollection* EmbeddedMeshCollection = nullptr;

		if (!MeshInfoMap.IsEmpty())
		{
			EmbeddedMeshCollection = NewObject<UPCGExMeshCollection>(OutAsset);
			EmbeddedMeshCollection->InitNumEntries(MeshInfoMap.Num());

			int32 MeshIdx = 0;
			for (auto& Elem : MeshInfoMap)
			{
				Elem.Value.EntryIndex = MeshIdx;

				FPCGExMeshCollectionEntry& MeshEntry = EmbeddedMeshCollection->Entries[MeshIdx];
				MeshEntry.StaticMesh = TSoftObjectPtr<UStaticMesh>(Elem.Key);
				MeshEntry.Weight = Elem.Value.TotalCount;

				// Populate ISM/SM descriptors from first source component
				if (Elem.Value.FirstComponent)
				{
					MeshEntry.ISMDescriptor.InitFrom(Elem.Value.FirstComponent, false);
					MeshEntry.SMDescriptor.InitFrom(Elem.Value.FirstComponent, false);
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

				MeshIdx++;
			}

			EmbeddedMeshCollection->RebuildStagingData(true);
		}

		// Build embedded actor collection
		UPCGExActorCollection* EmbeddedActorCollection = nullptr;

		if (!ActorClassInfoMap.IsEmpty())
		{
			EmbeddedActorCollection = NewObject<UPCGExActorCollection>(OutAsset);
			EmbeddedActorCollection->InitNumEntries(ActorClassInfoMap.Num());

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
				}

				ActorIdx++;
			}

			EmbeddedActorCollection->RebuildStagingData(true);
		}

		// Encode hashes on points
		PCGExCollections::FPickPacker Packer;

		// Encode mesh hashes
		if (MeshPointData && EmbeddedMeshCollection)
		{
			UPCGMetadata* Meta = MeshPointData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = MeshPointData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, int64(0), false, true);

			if (EntryHashAttr)
			{
				for (int32 i = 0; i < AllMeshPoints.Num(); i++)
				{
					const FMeshPoint& Point = AllMeshPoints[i];
					const FMeshInfo* Info = MeshInfoMap.Find(Point.MeshPath);
					if (!Info) { continue; }

					const int16 SecIdx = (bCaptureMaterialOverrides && Point.MaterialVariantIndex > 0)
						? static_cast<int16>(Point.MaterialVariantIndex) : static_cast<int16>(-1);

					const uint64 Hash = Packer.GetPickIdx(EmbeddedMeshCollection, static_cast<int16>(Info->EntryIndex), SecIdx);
					EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash));
				}
			}
		}

		// Encode actor hashes
		if (ActorPointData && EmbeddedActorCollection)
		{
			UPCGMetadata* Meta = ActorPointData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = ActorPointData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, int64(0), false, true);

			if (EntryHashAttr)
			{
				for (int32 i = 0; i < ActorActors.Num(); i++)
				{
					FActorInstanceKey Key;
					Key.ClassPath = FSoftClassPath(ActorActors[i].Actor->GetClass());
					Key.DeltaHash = ActorActors[i].DeltaHash;
					const FActorClassInfo* Info = ActorClassInfoMap.Find(Key);
					if (!Info) { continue; }

					const uint64 Hash = Packer.GetPickIdx(EmbeddedActorCollection, static_cast<int16>(Info->EntryIndex), static_cast<int16>(-1));
					EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash));
				}
			}
		}

		// Embed collection map
		UPCGParamData* MapData = NewObject<UPCGParamData>(OutAsset);
		Packer.PackToDataset(MapData);

		FPCGTaggedData& MapTaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		MapTaggedData.Data = MapData;
		MapTaggedData.Pin = FName(TEXT("CollectionMap"));
	}

	return OutAsset->Data.TaggedData.Num() > 0;
}
