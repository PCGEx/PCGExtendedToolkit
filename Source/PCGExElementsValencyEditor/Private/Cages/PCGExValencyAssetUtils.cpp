// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Cages/PCGExValencyAssetUtils.h"

#include "Cages/PCGExValencyCage.h"
#include "Engine/StaticMesh.h"
#include "Engine/Blueprint.h"
#include "PCGDataAsset.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

namespace PCGExValencyAssetUtils
{
	EPCGExValencyAssetType DetectAssetType(const TSoftObjectPtr<UObject>& Asset)
	{
		if (Asset.IsNull())
		{
			return EPCGExValencyAssetType::Unknown;
		}

		// Try to load to check type
		if (UObject* LoadedAsset = Asset.LoadSynchronous())
		{
			if (LoadedAsset->IsA<UStaticMesh>())
			{
				return EPCGExValencyAssetType::Mesh;
			}
			if (LoadedAsset->IsA<UBlueprint>())
			{
				return EPCGExValencyAssetType::Actor;
			}
			if (LoadedAsset->IsA<UPCGDataAsset>())
			{
				return EPCGExValencyAssetType::DataAsset;
			}
		}

		// Fallback: check path for common patterns
		const FString Path = Asset.ToSoftObjectPath().ToString();
		if (Path.Contains(TEXT("/StaticMesh")) || Path.EndsWith(TEXT("_SM")))
		{
			return EPCGExValencyAssetType::Mesh;
		}

		return EPCGExValencyAssetType::Unknown;
	}

	void ExtractMaterialOverrides(
		const UStaticMeshComponent* MeshComponent,
		TArray<FPCGExValencyMaterialOverride>& OutOverrides)
	{
		OutOverrides.Empty();

		if (!MeshComponent)
		{
			return;
		}

		const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
		if (!StaticMesh)
		{
			return;
		}

		const int32 NumMaterials = MeshComponent->GetNumMaterials();
		for (int32 SlotIndex = 0; SlotIndex < NumMaterials; ++SlotIndex)
		{
			UMaterialInterface* CurrentMaterial = MeshComponent->GetMaterial(SlotIndex);
			UMaterialInterface* DefaultMaterial = StaticMesh->GetMaterial(SlotIndex);

			// Only track if material differs from mesh's default
			if (CurrentMaterial && CurrentMaterial != DefaultMaterial)
			{
				FPCGExValencyMaterialOverride& Override = OutOverrides.AddDefaulted_GetRef();
				Override.SlotIndex = SlotIndex;
				Override.Material = CurrentMaterial;
			}
		}
	}

	void RecordMaterialVariant(
		const FSoftObjectPath& MeshPath,
		const TArray<FPCGExValencyMaterialOverride>& Overrides,
		TMap<FSoftObjectPath, TArray<FPCGExValencyMaterialVariant>>& OutVariantsMap)
	{
		if (Overrides.Num() == 0)
		{
			return;
		}

		// Find or create variants array for this mesh
		TArray<FPCGExValencyMaterialVariant>& Variants = OutVariantsMap.FindOrAdd(MeshPath);

		// Check if this exact configuration already exists
		FPCGExValencyMaterialVariant NewVariant;
		NewVariant.Overrides = Overrides;
		NewVariant.DiscoveryCount = 1;

		for (FPCGExValencyMaterialVariant& ExistingVariant : Variants)
		{
			if (ExistingVariant == NewVariant)
			{
				// Increment discovery count (becomes weight)
				ExistingVariant.DiscoveryCount++;
				return;
			}
		}

		// New unique variant
		Variants.Add(MoveTemp(NewVariant));
	}

	bool HaveScannedAssetsChanged(
		const TArray<FPCGExValencyAssetEntry>& OldScannedAssets,
		const TArray<FPCGExValencyAssetEntry>& NewScannedAssets,
		bool bCompareTransforms)
	{
		// Quick count check
		if (OldScannedAssets.Num() != NewScannedAssets.Num())
		{
			return true;
		}

		// Compare individual entries
		for (const FPCGExValencyAssetEntry& NewEntry : NewScannedAssets)
		{
			bool bFound = false;
			for (const FPCGExValencyAssetEntry& OldEntry : OldScannedAssets)
			{
				if (OldEntry.Asset == NewEntry.Asset)
				{
					// When transforms matter (preservation or connector strategy), also check transform equality
					if (bCompareTransforms)
					{
						if (OldEntry.LocalTransform.Equals(NewEntry.LocalTransform, 0.1f))
						{
							bFound = true;
							break;
						}
					}
					else
					{
						bFound = true;
						break;
					}
				}
			}
			if (!bFound)
			{
				return true;
			}
		}

		return false;
	}

	FTransform ComputePreservedLocalTransform(
		const FTransform& AssetWorldTransform,
		const FTransform& OwnerWorldTransform,
		bool bPreserveLocalTransforms,
		uint8 LocalTransformFlags,
		bool bConnectorTransformSensitive)
	{
		if (!bPreserveLocalTransforms && !bConnectorTransformSensitive)
		{
			return FTransform::Identity;
		}

		const FTransform LocalTransform = AssetWorldTransform.GetRelativeTransform(OwnerWorldTransform);

		// Connector strategy needs the full unfiltered transform for change detection
		if (!bPreserveLocalTransforms)
		{
			return LocalTransform;
		}

		// Apply flag-based filtering for the preservation feature
		FTransform Result = FTransform::Identity;

		const EPCGExLocalTransformFlags Flags = static_cast<EPCGExLocalTransformFlags>(LocalTransformFlags);

		if (EnumHasAnyFlags(Flags, EPCGExLocalTransformFlags::Translation))
		{
			Result.SetTranslation(LocalTransform.GetTranslation());
		}

		if (EnumHasAnyFlags(Flags, EPCGExLocalTransformFlags::Rotation))
		{
			Result.SetRotation(LocalTransform.GetRotation());
		}

		if (EnumHasAnyFlags(Flags, EPCGExLocalTransformFlags::Scale))
		{
			Result.SetScale3D(LocalTransform.GetScale3D());
		}

		return Result;
	}

	void FindMirroringCages(const AActor* Source, UWorld* World, TArray<APCGExValencyCage*>& OutCages)
	{
		OutCages.Empty();

		if (!Source || !World)
		{
			return;
		}

		// Find all cages that have this actor in their MirrorSources
		for (TActorIterator<APCGExValencyCage> It(World); It; ++It)
		{
			APCGExValencyCage* Cage = *It;
			if (!Cage || Cage == Source)
			{
				continue;
			}

			for (const FPCGExMirrorSource& MirrorEntry : Cage->MirrorSources)
			{
				if (MirrorEntry.Source == Source)
				{
					OutCages.Add(Cage);
					break;
				}
			}
		}
	}
}
