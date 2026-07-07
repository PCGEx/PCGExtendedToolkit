// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExAssetInspectionBlueprintLibrary.h"

#include "StaticMeshResources.h"
#include "AssetRegistry/AssetData.h"
#include "Core/PCGExAssetCollection.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#endif

namespace PCGExAssetInspectionBlueprintLibrary_Private
{
	int32 ClampLOD(const UStaticMesh* Mesh, int32 LOD)
	{
		return FMath::Clamp(LOD, 0, FMath::Max(0, Mesh->GetNumLODs() - 1));
	}

	const FStaticMeshLODResources* GetLODResources(const UStaticMesh* Mesh, int32 LOD)
	{
		const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
		if (!RenderData || RenderData->LODResources.IsEmpty())
		{
			return nullptr;
		}
		return &RenderData->LODResources[FMath::Clamp(LOD, 0, RenderData->LODResources.Num() - 1)];
	}
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshLODCount(const UStaticMesh* Mesh)
{
	return Mesh ? Mesh->GetNumLODs() : 0;
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshTriCount(const UStaticMesh* Mesh, int32 LOD)
{
	if (!Mesh || Mesh->GetNumLODs() == 0)
	{
		return 0;
	}
	return Mesh->GetNumTriangles(PCGExAssetInspectionBlueprintLibrary_Private::ClampLOD(Mesh, LOD));
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshVertexCount(const UStaticMesh* Mesh, int32 LOD)
{
	if (!Mesh || Mesh->GetNumLODs() == 0)
	{
		return 0;
	}
	return Mesh->GetNumVertices(PCGExAssetInspectionBlueprintLibrary_Private::ClampLOD(Mesh, LOD));
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshMaterialSlotCount(const UStaticMesh* Mesh)
{
	return Mesh ? Mesh->GetStaticMaterials().Num() : 0;
}

bool UPCGExAssetInspectionBlueprintLibrary::HasNaniteData(const UStaticMesh* Mesh)
{
	return Mesh ? Mesh->HasValidNaniteData() : false;
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshNaniteTriCount(const UStaticMesh* Mesh)
{
	return Mesh ? Mesh->GetNumNaniteTriangles() : 0;
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshNaniteVertexCount(const UStaticMesh* Mesh)
{
	return Mesh ? Mesh->GetNumNaniteVertices() : 0;
}

FBox UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshBounds(const UStaticMesh* Mesh)
{
	return Mesh ? Mesh->GetBoundingBox() : FBox(ForceInit);
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshSectionCount(const UStaticMesh* Mesh, int32 LOD)
{
	if (!Mesh || Mesh->GetNumLODs() == 0)
	{
		return 0;
	}
	return Mesh->GetNumSections(PCGExAssetInspectionBlueprintLibrary_Private::ClampLOD(Mesh, LOD));
}

float UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshLODScreenSize(const UStaticMesh* Mesh, int32 LOD)
{
	const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
	if (!RenderData || RenderData->LODResources.IsEmpty())
	{
		return 0.0f;
	}
	return RenderData->ScreenSize[FMath::Clamp(LOD, 0, RenderData->LODResources.Num() - 1)].GetValue();
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshUVChannelCount(const UStaticMesh* Mesh, int32 LOD)
{
	const FStaticMeshLODResources* LODResources = PCGExAssetInspectionBlueprintLibrary_Private::GetLODResources(Mesh, LOD);
	return LODResources ? static_cast<int32>(LODResources->VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()) : 0;
}

bool UPCGExAssetInspectionBlueprintLibrary::HasVertexColors(const UStaticMesh* Mesh)
{
	const FStaticMeshLODResources* LODResources = PCGExAssetInspectionBlueprintLibrary_Private::GetLODResources(Mesh, 0);
	return LODResources ? LODResources->VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0 : false;
}

bool UPCGExAssetInspectionBlueprintLibrary::HasDistanceField(const UStaticMesh* Mesh)
{
	const FStaticMeshLODResources* LODResources = PCGExAssetInspectionBlueprintLibrary_Private::GetLODResources(Mesh, 0);
	return LODResources ? LODResources->DistanceFieldData != nullptr : false;
}

TArray<FName> UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshMaterialSlotNames(const UStaticMesh* Mesh)
{
	TArray<FName> SlotNames;
	if (!Mesh)
	{
		return SlotNames;
	}

	const TArray<FStaticMaterial>& StaticMaterials = Mesh->GetStaticMaterials();
	SlotNames.Reserve(StaticMaterials.Num());
	for (const FStaticMaterial& StaticMaterial : StaticMaterials)
	{
		SlotNames.Add(StaticMaterial.MaterialSlotName);
	}
	return SlotNames;
}

int32 UPCGExAssetInspectionBlueprintLibrary::FindStaticMeshMaterialSlotIndex(const UStaticMesh* Mesh, FName SlotName)
{
	return Mesh ? Mesh->GetMaterialIndex(SlotName) : INDEX_NONE;
}

UMaterialInterface* UPCGExAssetInspectionBlueprintLibrary::GetStaticMeshMaterial(const UStaticMesh* Mesh, int32 SlotIndex)
{
	return Mesh ? Mesh->GetMaterial(SlotIndex) : nullptr;
}

bool UPCGExAssetInspectionBlueprintLibrary::HasSimpleCollision(const UStaticMesh* Mesh)
{
	return GetCollisionPrimitiveCount(Mesh) > 0;
}

int32 UPCGExAssetInspectionBlueprintLibrary::GetCollisionPrimitiveCount(const UStaticMesh* Mesh)
{
	const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
	return BodySetup ? BodySetup->AggGeom.GetElementCount() : 0;
}

bool UPCGExAssetInspectionBlueprintLibrary::UsesComplexAsSimple(const UStaticMesh* Mesh)
{
	const UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
	return BodySetup ? BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple : false;
}

int64 UPCGExAssetInspectionBlueprintLibrary::GetAssetResourceSizeBytes(UObject* Asset)
{
	return Asset ? static_cast<int64>(Asset->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal)) : 0;
}

bool UPCGExAssetInspectionBlueprintLibrary::GetAssetRegistryTag(const FSoftObjectPath& AssetPath, FName TagName, FString& OutValue)
{
	OutValue.Reset();

#if WITH_EDITOR
	if (!AssetPath.IsValid() || TagName.IsNone())
	{
		return false;
	}

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(AssetPath, true);
	if (!AssetData.IsValid())
	{
		return false;
	}

	return AssetData.GetTagValue(TagName, OutValue);
#else
	return false;
#endif
}

bool UPCGExAssetInspectionBlueprintLibrary::GetEntryAssetRegistryTag(const UPCGExAssetCollection* Collection, int32 EntryIndex, FName TagName, FString& OutValue)
{
	OutValue.Reset();

	if (!Collection)
	{
		return false;
	}

	const FPCGExEntryAccessResult Result = Collection->GetEntryRaw(EntryIndex);
	if (!Result)
	{
		return false;
	}

	return GetAssetRegistryTag(Result.Entry->Staging.Path, TagName, OutValue);
}
