// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Thumbnails/PCGExPCGDataAssetThumbnailRenderer.h"

#include "PCGDataAsset.h"
#include "PCGExCollectionsCommon.h"
#include "PCGParamData.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "ShowFlags.h"
#include "ThumbnailHelpers.h"
#include "Collections/PCGExMeshCollection.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Core/PCGExAssetCollection.h"
#include "Data/PCGBasePointData.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExCollectionsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"

namespace PCGExPCGDataAssetThumbnail
{
	/** Instances past this are dropped: a thumbnail is a glance, not a render of a city. */
	constexpr int32 MaxInstances = 4096;

	/** Every mesh point of the asset, grouped by resolved mesh. Empty when nothing resolves. */
	void GatherMeshInstances(const UPCGDataAsset* Asset, TMap<UStaticMesh*, TArray<FTransform>>& OutInstances)
	{
		OutInstances.Reset();
		if (!Asset)
		{
			return;
		}

		// Collections first: the CollectionMap pin lists every collection the point hashes resolve
		// through. Loaded here rather than through the unpacker's own reader, which logs through a
		// PCG context this renderer does not have.
		PCGExCollections::FPickUnpacker Unpacker;
		for (const FPCGTaggedData& TaggedData : Asset->Data.TaggedData)
		{
			if (TaggedData.Pin != PCGExCollections::Labels::CollectionMapPin)
			{
				continue;
			}
			const UPCGParamData* MapData = Cast<UPCGParamData>(TaggedData.Data);
			if (!MapData || !MapData->Metadata)
			{
				continue;
			}
			const FPCGMetadataAttribute<FSoftObjectPath>* PathAttr = MapData->Metadata->GetConstTypedAttribute<FSoftObjectPath>(PCGExCollections::Labels::Tag_CollectionPath);
			if (!PathAttr)
			{
				continue;
			}
			const int64 NumRows = MapData->Metadata->GetItemCountForChild();
			for (int64 Row = 0; Row < NumRows; Row++)
			{
				const FSoftObjectPath Path = PathAttr->GetValueFromItemKey(Row);
				if (UPCGExAssetCollection* Collection = Cast<UPCGExAssetCollection>(Path.TryLoad()))
				{
					Unpacker.AddCollection(Collection);
				}
			}
		}

		int32 Budget = MaxInstances;
		for (const FPCGTaggedData& TaggedData : Asset->Data.TaggedData)
		{
			if (TaggedData.Pin != PCGExCollections::Labels::MeshesPin)
			{
				continue;
			}
			const UPCGBasePointData* Points = Cast<UPCGBasePointData>(TaggedData.Data);
			if (!Points || Points->GetNumPoints() == 0)
			{
				continue;
			}

			const UPCGMetadata* Metadata = Points->ConstMetadata();
			const FPCGMetadataAttribute<int64>* HashAttr = Metadata ? Metadata->GetConstTypedAttribute<int64>(PCGExCollections::Labels::Tag_EntryIdx) : nullptr;
			const FPCGMetadataAttribute<FSoftObjectPath>* MeshAttr = Metadata ? Metadata->GetConstTypedAttribute<FSoftObjectPath>(TEXT("Mesh")) : nullptr;
			if (!HashAttr && !MeshAttr)
			{
				continue;
			}

			const TConstPCGValueRange<FTransform> Transforms = Points->GetConstTransformValueRange();
			const TConstPCGValueRange<int64> MetadataEntries = Points->GetConstMetadataEntryValueRange();
			const int32 NumPoints = Points->GetNumPoints();

			for (int32 i = 0; i < NumPoints && Budget > 0; i++)
			{
				UStaticMesh* Mesh = nullptr;
				const int64 MetadataEntry = MetadataEntries[i];

				if (HashAttr && Unpacker.HasValidMapping())
				{
					const int64 Hash = HashAttr->GetValueFromItemKey(MetadataEntry);
					if (Hash != 0 && Hash != -1)
					{
						int16 SecondaryIndex = 0;
						const FPCGExEntryAccessResult Result = Unpacker.ResolveEntry(static_cast<uint64>(Hash), SecondaryIndex);
						if (Result.IsValid() && Result.Entry->IsType(PCGExAssetCollection::TypeIds::Mesh))
						{
							Mesh = static_cast<const FPCGExMeshCollectionEntry*>(Result.Entry)->StaticMesh.LoadSynchronous();
						}
					}
				}
				if (!Mesh && MeshAttr)
				{
					Mesh = Cast<UStaticMesh>(MeshAttr->GetValueFromItemKey(MetadataEntry).TryLoad());
				}
				if (!Mesh)
				{
					continue;
				}

				OutInstances.FindOrAdd(Mesh).Add(Transforms[i]);
				Budget--;
			}
		}
	}
}

/**
 * Preview scene holding one transient actor with one ISM component per mesh. Rebuilt per draw:
 * SetDataAsset populates, Clear tears the components down.
 */
class FPCGExDataAssetThumbnailScene final : public FThumbnailPreviewScene
{
public:
	FPCGExDataAssetThumbnailScene()
		: FThumbnailPreviewScene()
	{
		bForceAllUsedMipsResident = false;

		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.bNoFail = true;
		SpawnInfo.ObjectFlags = RF_Transient;
		AActor* Actor = GetWorld()->SpawnActor<AActor>(SpawnInfo);

		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"), RF_Transient);
		Root->SetMobility(EComponentMobility::Movable);
		Actor->SetRootComponent(Root);
		Root->RegisterComponentWithWorld(GetWorld());
		Actor->SetActorEnableCollision(false);

		PreviewActor = Actor;
	}

	bool SetDataAsset(const UPCGDataAsset* Asset)
	{
		Clear();

		AActor* Actor = PreviewActor.Get();
		if (!Actor || !Actor->GetRootComponent())
		{
			return false;
		}

		TMap<UStaticMesh*, TArray<FTransform>> Instances;
		PCGExPCGDataAssetThumbnail::GatherMeshInstances(Asset, Instances);
		if (Instances.IsEmpty())
		{
			return false;
		}

		FBoxSphereBounds::Builder BoundsBuilder;
		for (const TPair<UStaticMesh*, TArray<FTransform>>& Pair : Instances)
		{
			UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transient);
			ISM->SetStaticMesh(Pair.Key);
			ISM->SetMobility(EComponentMobility::Movable);
			ISM->SetCanEverAffectNavigation(false);
			ISM->bSelectable = false;
			ISM->ForcedLodModel = 1;
			ISM->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
			ISM->RegisterComponentWithWorld(GetWorld());
			ISM->AddInstances(Pair.Value, /*bShouldReturnIndices=*/false);
			ISM->UpdateBounds();
			BoundsBuilder += ISM->Bounds;
			Components.Add(ISM);
		}

		CachedBounds = BoundsBuilder;

		// Centre the assembly at the origin, resting on the floor plane, the way every engine scene does.
		Actor->SetActorLocation(-CachedBounds.Origin + FVector(0, 0, GetBoundsZOffset(CachedBounds)), false);
		for (UInstancedStaticMeshComponent* ISM : Components)
		{
			ISM->RecreateRenderState_Concurrent();
		}
		return true;
	}

	void Clear()
	{
		for (UInstancedStaticMeshComponent* ISM : Components)
		{
			if (ISM)
			{
				ISM->DestroyComponent();
			}
		}
		Components.Reset();
		CachedBounds = FBoxSphereBounds(ForceInit);
	}

protected:
	virtual void GetViewMatrixParameters(const float InFOVDegrees, FVector& OutOrigin, float& OutOrbitPitch, float& OutOrbitYaw, float& OutOrbitZoom) const override
	{
		const float HalfFOVRadians = FMath::DegreesToRadians<float>(InFOVDegrees) * 0.5f;
		// Slightly outside the sphere to compensate for perspective -- the engine's own factor.
		const float HalfMeshSize = static_cast<float>(CachedBounds.SphereRadius * 1.15);
		const float BoundsZOffset = GetBoundsZOffset(CachedBounds);
		const float TargetDistance = HalfMeshSize / FMath::Tan(HalfFOVRadians);

		const USceneThumbnailInfo* ThumbnailInfo = USceneThumbnailInfo::StaticClass()->GetDefaultObject<USceneThumbnailInfo>();

		OutOrigin = FVector(0, 0, -BoundsZOffset);
		OutOrbitPitch = ThumbnailInfo->OrbitPitch;
		OutOrbitYaw = ThumbnailInfo->OrbitYaw;
		OutOrbitZoom = TargetDistance + ThumbnailInfo->OrbitZoom;
	}

private:
	TWeakObjectPtr<AActor> PreviewActor;
	TArray<UInstancedStaticMeshComponent*> Components;
	FBoxSphereBounds CachedBounds = FBoxSphereBounds(ForceInit);
};

#pragma region UPCGExPCGDataAssetThumbnailRenderer

bool UPCGExPCGDataAssetThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	const UPCGDataAsset* Asset = Cast<UPCGDataAsset>(Object);
	if (!Asset)
	{
		return false;
	}
	for (const FPCGTaggedData& TaggedData : Asset->Data.TaggedData)
	{
		if (TaggedData.Pin != PCGExCollections::Labels::MeshesPin)
		{
			continue;
		}
		if (const UPCGBasePointData* Points = Cast<UPCGBasePointData>(TaggedData.Data); Points && Points->GetNumPoints() > 0)
		{
			return true;
		}
	}
	return false;
}

void UPCGExPCGDataAssetThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const UPCGDataAsset* Asset = Cast<UPCGDataAsset>(Object);
	if (!IsValid(Asset))
	{
		return;
	}

	if (ThumbnailScene == nullptr || ensure(ThumbnailScene->GetWorld() != nullptr) == false)
	{
		if (ThumbnailScene)
		{
			FlushRenderingCommands();
			delete ThumbnailScene;
		}
		ThumbnailScene = new FPCGExDataAssetThumbnailScene();
	}

	if (!ThumbnailScene->SetDataAsset(Asset))
	{
		return;
	}

	ThumbnailScene->GetScene()->UpdateSpeedTreeWind(0.0);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTarget, ThumbnailScene->GetScene(), FEngineShowFlags(ESFIM_Game))
		.SetTime(UThumbnailRenderer::GetTime())
		.SetAdditionalViewFamily(bAdditionalViewFamily));

	ViewFamily.EngineShowFlags.DisableAdvancedFeatures();
	ViewFamily.EngineShowFlags.MotionBlur = 0;
	ViewFamily.EngineShowFlags.LOD = 0;

	RenderViewFamily(Canvas, &ViewFamily, ThumbnailScene->CreateView(&ViewFamily, X, Y, Width, Height));
	ThumbnailScene->Clear();
}

void UPCGExPCGDataAssetThumbnailRenderer::BeginDestroy()
{
	if (ThumbnailScene != nullptr)
	{
		delete ThumbnailScene;
		ThumbnailScene = nullptr;
	}
	Super::BeginDestroy();
}

#pragma endregion
