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

	// Phase 2: Create typed point data

	// --- Meshes ---
	if (!MeshActors.IsEmpty())
	{
		TPCGValueRange<FTransform> Transforms;
		TPCGValueRange<FVector> BMin, BMax;
		UPCGBasePointData* MeshData = CreatePointData(OutAsset, MeshActors.Num(), Transforms, BMin, BMax);

		for (int32 i = 0; i < MeshActors.Num(); i++)
		{
			WriteActorTransformAndBounds(MeshActors[i].Actor, i, Transforms, BMin, BMax);
		}

		InitMetadata(MeshData, MeshActors.Num());

		// Write metadata attributes
		if (!bGenerateCollections)
		{
			UPCGMetadata* Meta = MeshData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = MeshData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);
			FPCGMetadataAttribute<FSoftObjectPath>* MeshAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, true);

			for (int32 i = 0; i < MeshActors.Num(); i++)
			{
				const int64 Entry = MetaEntries[i];
				if (ActorNameAttr) { ActorNameAttr->SetValue(Entry, MeshActors[i].Actor->GetActorNameOrLabel()); }
				if (MeshAttr && MeshActors[i].MeshComponent)
				{
					if (UStaticMesh* Mesh = MeshActors[i].MeshComponent->GetStaticMesh())
					{
						MeshAttr->SetValue(Entry, FSoftObjectPath(Mesh));
					}
				}
			}
		}
		else
		{
			// Collection mode: only write ActorName, hash written later in Phase 3
			UPCGMetadata* Meta = MeshData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = MeshData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);
			for (int32 i = 0; i < MeshActors.Num(); i++)
			{
				if (ActorNameAttr) { ActorNameAttr->SetValue(MetaEntries[i], MeshActors[i].Actor->GetActorNameOrLabel()); }
			}
		}

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = MeshData;
		TaggedData.Pin = FName(TEXT("Meshes"));
	}

	// --- Actors ---
	if (!ActorActors.IsEmpty())
	{
		TPCGValueRange<FTransform> Transforms;
		TPCGValueRange<FVector> BMin, BMax;
		UPCGBasePointData* ActorData = CreatePointData(OutAsset, ActorActors.Num(), Transforms, BMin, BMax);

		for (int32 i = 0; i < ActorActors.Num(); i++)
		{
			WriteActorTransformAndBounds(ActorActors[i].Actor, i, Transforms, BMin, BMax);
		}

		InitMetadata(ActorData, ActorActors.Num());

		if (!bGenerateCollections)
		{
			UPCGMetadata* Meta = ActorData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = ActorData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);
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
			UPCGMetadata* Meta = ActorData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = ActorData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);
			for (int32 i = 0; i < ActorActors.Num(); i++)
			{
				if (ActorNameAttr) { ActorNameAttr->SetValue(MetaEntries[i], ActorActors[i].Actor->GetActorNameOrLabel()); }
			}
		}

		FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
		TaggedData.Data = ActorData;
		TaggedData.Pin = FName(TEXT("Actors"));
	}

	// --- ISM Instances (unchanged behavior) ---
	for (const FClassifiedActor& CA : ClassifiedActors)
	{
		TArray<UInstancedStaticMeshComponent*> ISMComponents;
		CA.Actor->GetComponents<UInstancedStaticMeshComponent>(ISMComponents);

		for (const UInstancedStaticMeshComponent* ISM : ISMComponents)
		{
			if (!ISM || ISM->GetInstanceCount() == 0) { continue; }

			UStaticMesh* Mesh = ISM->GetStaticMesh();
			if (!Mesh) { continue; }

			const int32 InstanceCount = ISM->GetInstanceCount();

			const FBox MeshBounds = Mesh->GetBoundingBox();
			const FVector MeshBoundsMin = MeshBounds.Min;
			const FVector MeshBoundsMax = MeshBounds.Max;

			TPCGValueRange<FTransform> InstanceTransforms;
			TPCGValueRange<FVector> InstanceBMin, InstanceBMax;
			UPCGBasePointData* InstanceData = CreatePointData(OutAsset, InstanceCount, InstanceTransforms, InstanceBMin, InstanceBMax);

			for (int32 Idx = 0; Idx < InstanceCount; Idx++)
			{
				FTransform InstanceTransform;
				ISM->GetInstanceTransform(Idx, InstanceTransform, true);
				InstanceTransforms[Idx] = InstanceTransform;
				InstanceBMin[Idx] = MeshBoundsMin;
				InstanceBMax[Idx] = MeshBoundsMax;
			}

			InitMetadata(InstanceData, InstanceCount);

			if (!bGenerateCollections)
			{
				UPCGMetadata* Meta = InstanceData->MutableMetadata();
				FPCGMetadataAttribute<FSoftObjectPath>* InstanceMeshAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, true);
				if (InstanceMeshAttr)
				{
					TPCGValueRange<int64> MetaEntries = InstanceData->GetMetadataEntryValueRange();
					const FSoftObjectPath MeshPath(Mesh);
					for (int32 Idx = 0; Idx < InstanceCount; Idx++)
					{
						InstanceMeshAttr->SetValue(MetaEntries[Idx], MeshPath);
					}
				}
			}

			FPCGTaggedData& InstanceTaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
			InstanceTaggedData.Data = InstanceData;
			InstanceTaggedData.Pin = FName(TEXT("Instances"));
			InstanceTaggedData.Tags.Add(Mesh->GetPathName());
		}
	}

	// Phase 2.5: Notify subclasses
	OnExportComplete(OutAsset);

	// Phase 3: Embedded collection generation (when bGenerateCollections)
	if (bGenerateCollections)
	{
		// Scan unique meshes from Meshes + Instances
		TMap<FSoftObjectPath, int32> UniqueMeshes;

		for (const FClassifiedActor& CA : MeshActors)
		{
			if (CA.MeshComponent)
			{
				if (UStaticMesh* Mesh = CA.MeshComponent->GetStaticMesh())
				{
					UniqueMeshes.FindOrAdd(FSoftObjectPath(Mesh))++;
				}
			}
		}

		// Also scan ISM meshes
		for (const FClassifiedActor& CA : ClassifiedActors)
		{
			TArray<UInstancedStaticMeshComponent*> ISMComponents;
			CA.Actor->GetComponents<UInstancedStaticMeshComponent>(ISMComponents);
			for (const UInstancedStaticMeshComponent* ISM : ISMComponents)
			{
				if (!ISM || ISM->GetInstanceCount() == 0) { continue; }
				if (UStaticMesh* Mesh = ISM->GetStaticMesh())
				{
					UniqueMeshes.FindOrAdd(FSoftObjectPath(Mesh)) += ISM->GetInstanceCount();
				}
			}
		}

		// Scan unique actor classes
		TMap<FSoftClassPath, int32> UniqueActors;
		for (const FClassifiedActor& CA : ActorActors)
		{
			UniqueActors.FindOrAdd(FSoftClassPath(CA.Actor->GetClass()))++;
		}

		// Build embedded mesh collection
		UPCGExMeshCollection* EmbeddedMeshCollection = nullptr;
		TMap<FSoftObjectPath, int32> MeshPathToEntryIndex;

		if (!UniqueMeshes.IsEmpty())
		{
			EmbeddedMeshCollection = NewObject<UPCGExMeshCollection>(OutAsset);

			EmbeddedMeshCollection->InitNumEntries(UniqueMeshes.Num());
			int32 MeshIdx = 0;
			for (const auto& Pair : UniqueMeshes)
			{
				MeshPathToEntryIndex.Add(Pair.Key, MeshIdx);

				FPCGExMeshCollectionEntry& MeshEntry = EmbeddedMeshCollection->Entries[MeshIdx];
				MeshEntry.StaticMesh = TSoftObjectPtr<UStaticMesh>(Pair.Key);
				MeshEntry.Weight = Pair.Value;
				MeshIdx++;
			}
			EmbeddedMeshCollection->RebuildStagingData(true);
		}

		// Build embedded actor collection
		UPCGExActorCollection* EmbeddedActorCollection = nullptr;
		TMap<FSoftClassPath, int32> ActorClassToEntryIndex;

		if (!UniqueActors.IsEmpty())
		{
			EmbeddedActorCollection = NewObject<UPCGExActorCollection>(OutAsset);

			EmbeddedActorCollection->InitNumEntries(UniqueActors.Num());
			int32 ActorIdx = 0;
			for (const auto& Pair : UniqueActors)
			{
				ActorClassToEntryIndex.Add(Pair.Key, ActorIdx);

				FPCGExActorCollectionEntry& ActorEntry = EmbeddedActorCollection->Entries[ActorIdx];
				ActorEntry.Actor = TSoftClassPtr<AActor>(Pair.Key);
				ActorEntry.Weight = Pair.Value;
				ActorIdx++;
			}
			EmbeddedActorCollection->RebuildStagingData(true);
		}

		// Encode hashes on points
		PCGExCollections::FPickPacker Packer;

		// Encode mesh hashes on Meshes data
		for (FPCGTaggedData& TD : OutAsset->Data.TaggedData)
		{
			if (TD.Pin != FName(TEXT("Meshes")) || !EmbeddedMeshCollection) { continue; }

			UPCGBasePointData* MeshData = const_cast<UPCGBasePointData*>(Cast<UPCGBasePointData>(TD.Data));
			if (!MeshData) { continue; }

			UPCGMetadata* Meta = MeshData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = MeshData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, int64(0), false, true);

			for (int32 i = 0; i < MeshActors.Num(); i++)
			{
				if (!MeshActors[i].MeshComponent) { continue; }
				UStaticMesh* Mesh = MeshActors[i].MeshComponent->GetStaticMesh();
				if (!Mesh) { continue; }

				const int32* EntryIdx = MeshPathToEntryIndex.Find(FSoftObjectPath(Mesh));
				if (!EntryIdx) { continue; }

				const uint64 Hash = Packer.GetPickIdx(EmbeddedMeshCollection, *EntryIdx, -1);
				if (EntryHashAttr) { EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash)); }
			}
		}

		// Encode mesh hashes on Instances data
		for (FPCGTaggedData& TD : OutAsset->Data.TaggedData)
		{
			if (TD.Pin != FName(TEXT("Instances")) || !EmbeddedMeshCollection) { continue; }

			UPCGBasePointData* InstanceData = const_cast<UPCGBasePointData*>(Cast<UPCGBasePointData>(TD.Data));
			if (!InstanceData) { continue; }

			// Get the mesh path from the tag
			FSoftObjectPath MeshPath;
			for (const FString& Tag : TD.Tags)
			{
				MeshPath = FSoftObjectPath(Tag);
				break; // First tag is the mesh path
			}

			const int32* EntryIdx = MeshPathToEntryIndex.Find(MeshPath);
			if (!EntryIdx) { continue; }

			const uint64 Hash = Packer.GetPickIdx(EmbeddedMeshCollection, *EntryIdx, -1);

			UPCGMetadata* Meta = InstanceData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = InstanceData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, int64(0), false, true);

			if (EntryHashAttr)
			{
				const int32 NumInstances = InstanceData->GetNumPoints();
				for (int32 Idx = 0; Idx < NumInstances; Idx++)
				{
					EntryHashAttr->SetValue(MetaEntries[Idx], static_cast<int64>(Hash));
				}
			}
		}

		// Encode actor hashes on Actors data
		for (FPCGTaggedData& TD : OutAsset->Data.TaggedData)
		{
			if (TD.Pin != FName(TEXT("Actors")) || !EmbeddedActorCollection) { continue; }

			UPCGBasePointData* ActorData = const_cast<UPCGBasePointData*>(Cast<UPCGBasePointData>(TD.Data));
			if (!ActorData) { continue; }

			UPCGMetadata* Meta = ActorData->MutableMetadata();
			TPCGValueRange<int64> MetaEntries = ActorData->GetMetadataEntryValueRange();

			FPCGMetadataAttribute<int64>* EntryHashAttr = Meta->CreateAttribute<int64>(
				PCGExCollections::Labels::Tag_EntryIdx, int64(0), false, true);

			for (int32 i = 0; i < ActorActors.Num(); i++)
			{
				const int32* EntryIdx = ActorClassToEntryIndex.Find(FSoftClassPath(ActorActors[i].Actor->GetClass()));
				if (!EntryIdx) { continue; }

				const uint64 Hash = Packer.GetPickIdx(EmbeddedActorCollection, *EntryIdx, -1);
				if (EntryHashAttr) { EntryHashAttr->SetValue(MetaEntries[i], static_cast<int64>(Hash)); }
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
