// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExDefaultLevelDataExporter.h"

#include "PCGDataAsset.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGExPointArrayDataHelpers.h"

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

bool UPCGExDefaultLevelDataExporter::ExportLevelData_Implementation(UWorld* World, UPCGDataAsset* OutAsset)
{
	if (!World || !OutAsset) { return false; }

	ULevel* PersistentLevel = World->PersistentLevel;
	if (!PersistentLevel) { return false; }

	// Collect qualifying actors
	TArray<AActor*> QualifyingActors;
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

		QualifyingActors.Add(Actor);
	}

	if (QualifyingActors.IsEmpty()) { return false; }

	// Create point data for actor transforms — outered to the asset for serialization
	UPCGPointData* PointData = NewObject<UPCGPointData>(OutAsset);
	PCGExPointArrayDataHelpers::SetNumPointsAllocated(
		PointData, QualifyingActors.Num(),
		EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

	// Get native value ranges for transforms and per-point local-space bounds
	TPCGValueRange<FTransform> Transforms = PointData->GetTransformValueRange();
	TPCGValueRange<FVector> BoundsMin = PointData->GetBoundsMinValueRange();
	TPCGValueRange<FVector> BoundsMax = PointData->GetBoundsMaxValueRange();

	for (int32 i = 0; i < QualifyingActors.Num(); i++)
	{
		AActor* Actor = QualifyingActors[i];
		const FTransform ActorTransform = Actor->GetActorTransform();
		Transforms[i] = ActorTransform;

		// Compute local-space bounds: world bounds transformed back into actor-local space
		FVector Origin, BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent);

		// Origin/BoxExtent are world-space center+half-extent.
		// Convert to local space relative to the actor's transform.
		const FTransform InvTransform = ActorTransform.Inverse();
		const FVector LocalCenter = InvTransform.TransformPosition(Origin);

		BoundsMin[i] = LocalCenter - BoxExtent;
		BoundsMax[i] = LocalCenter + BoxExtent;
	}

	// Initialize metadata entries so attributes can be written per-point
	{
		UPCGMetadata* Meta = PointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = PointData->GetMetadataEntryValueRange();

		TArray<TTuple<int64, int64>> DelayedEntries;
		DelayedEntries.SetNum(QualifyingActors.Num());

		for (int32 i = 0; i < QualifyingActors.Num(); i++)
		{
			MetaEntries[i] = Meta->AddEntryPlaceholder();
			DelayedEntries[i] = MakeTuple(MetaEntries[i], int64(-1));
		}

		Meta->AddDelayedEntries(DelayedEntries);
	}

	// Write metadata: actor name, mesh path
	{
		UPCGMetadata* Meta = PointData->MutableMetadata();
		TPCGValueRange<int64> MetaEntries = PointData->GetMetadataEntryValueRange();

		FPCGMetadataAttribute<FString>* ActorNameAttr = Meta->CreateAttribute<FString>(TEXT("ActorName"), FString(), false, true);
		FPCGMetadataAttribute<FSoftObjectPath>* MeshAttr = Meta->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, true);

		for (int32 i = 0; i < QualifyingActors.Num(); i++)
		{
			AActor* Actor = QualifyingActors[i];
			const int64 Entry = MetaEntries[i];

			if (ActorNameAttr) { ActorNameAttr->SetValue(Entry, Actor->GetActorNameOrLabel()); }

			// Mesh from first static mesh component
			if (const UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>())
			{
				if (UStaticMesh* Mesh = SMC->GetStaticMesh())
				{
					if (MeshAttr) { MeshAttr->SetValue(Entry, FSoftObjectPath(Mesh)); }
				}
			}
		}
	}

	// Add to the data asset's tagged data
	FPCGTaggedData& TaggedData = OutAsset->Data.TaggedData.Emplace_GetRef();
	TaggedData.Data = PointData;
	TaggedData.Pin = FName(TEXT("Points"));

	// Export ISM instances as separate tagged data entries
	for (AActor* Actor : QualifyingActors)
	{
		TArray<UInstancedStaticMeshComponent*> ISMComponents;
		Actor->GetComponents<UInstancedStaticMeshComponent>(ISMComponents);

		for (const UInstancedStaticMeshComponent* ISM : ISMComponents)
		{
			if (!ISM || ISM->GetInstanceCount() == 0) { continue; }

			UStaticMesh* Mesh = ISM->GetStaticMesh();
			if (!Mesh) { continue; }

			const int32 InstanceCount = ISM->GetInstanceCount();

			// Get mesh local bounds for all instances of this ISM
			const FBox MeshBounds = Mesh->GetBoundingBox();
			const FVector MeshBoundsMin = MeshBounds.Min;
			const FVector MeshBoundsMax = MeshBounds.Max;

			UPCGPointData* InstanceData = NewObject<UPCGPointData>(OutAsset);
			PCGExPointArrayDataHelpers::SetNumPointsAllocated(
				InstanceData, InstanceCount,
				EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::BoundsMin | EPCGPointNativeProperties::BoundsMax);

			TPCGValueRange<FTransform> InstanceTransforms = InstanceData->GetTransformValueRange();
			TPCGValueRange<FVector> InstanceBoundsMin = InstanceData->GetBoundsMinValueRange();
			TPCGValueRange<FVector> InstanceBoundsMax = InstanceData->GetBoundsMaxValueRange();

			for (int32 Idx = 0; Idx < InstanceCount; Idx++)
			{
				FTransform InstanceTransform;
				ISM->GetInstanceTransform(Idx, InstanceTransform, true);
				InstanceTransforms[Idx] = InstanceTransform;
				InstanceBoundsMin[Idx] = MeshBoundsMin;
				InstanceBoundsMax[Idx] = MeshBoundsMax;
			}

			// Init metadata entries
			{
				UPCGMetadata* Meta = InstanceData->MutableMetadata();
				TPCGValueRange<int64> MetaEntries = InstanceData->GetMetadataEntryValueRange();

				TArray<TTuple<int64, int64>> DelayedEntries;
				DelayedEntries.SetNum(InstanceCount);

				for (int32 Idx = 0; Idx < InstanceCount; Idx++)
				{
					MetaEntries[Idx] = Meta->AddEntryPlaceholder();
					DelayedEntries[Idx] = MakeTuple(MetaEntries[Idx], int64(-1));
				}

				Meta->AddDelayedEntries(DelayedEntries);
			}

			// Write mesh path attribute on instance data
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

	return OutAsset->Data.TaggedData.Num() > 0;
}
