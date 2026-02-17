// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Cages/PCGExValencyAssetContainerBase.h"

#include "Editor.h"
#include "Cages/PCGExValencyAssetUtils.h"
#include "PCGExValencyMacros.h"

APCGExValencyAssetContainerBase::APCGExValencyAssetContainerBase()
{
	ConnectorTransformStrategy.InitializeAs<FPCGExConnectorTransform_AssetRelative>();
}

#pragma region Asset Accessors

TArray<FPCGExValencyAssetEntry> APCGExValencyAssetContainerBase::GetAllAssetEntries() const
{
	TArray<FPCGExValencyAssetEntry> AllEntries;
	AllEntries.Reserve(ManualAssetEntries.Num() + ScannedAssetEntries.Num());

	// Manual entries first (user-defined priority)
	AllEntries.Append(ManualAssetEntries);

	// Then scanned entries (skip duplicates of manual)
	for (const FPCGExValencyAssetEntry& ScannedEntry : ScannedAssetEntries)
	{
		bool bIsDuplicate = false;
		for (const FPCGExValencyAssetEntry& ManualEntry : ManualAssetEntries)
		{
			if (ManualEntry.Asset == ScannedEntry.Asset)
			{
				bIsDuplicate = true;
				break;
			}
		}
		if (!bIsDuplicate)
		{
			AllEntries.Add(ScannedEntry);
		}
	}

	// Stamp container's ModuleSettings onto each entry
	// This allows entries to carry their source's weight/constraints through mirroring
	for (FPCGExValencyAssetEntry& Entry : AllEntries)
	{
		Entry.Settings = ModuleSettings;
		Entry.bHasSettings = true;
	}

	return AllEntries;
}

#pragma endregion

#pragma region Transform and Comparison

bool APCGExValencyAssetContainerBase::IsConnectorTransformSensitive() const
{
	if (const FPCGExConnectorTransformStrategy* Strategy = ConnectorTransformStrategy.GetPtr<FPCGExConnectorTransformStrategy>())
	{
		return Strategy->IsTransformSensitive();
	}
	return false;
}

FTransform APCGExValencyAssetContainerBase::ComputePreservedLocalTransform(const FTransform& AssetWorldTransform) const
{
	return PCGExValencyAssetUtils::ComputePreservedLocalTransform(
		AssetWorldTransform, GetActorTransform(), bPreserveLocalTransforms, LocalTransformFlags,
		IsConnectorTransformSensitive());
}

bool APCGExValencyAssetContainerBase::HaveScannedAssetsChanged(const TArray<FPCGExValencyAssetEntry>& OldScannedAssets) const
{
	return PCGExValencyAssetUtils::HaveScannedAssetsChanged(
		OldScannedAssets, ScannedAssetEntries,
		bPreserveLocalTransforms || IsConnectorTransformSensitive());
}

#pragma endregion

#pragma region Mirroring

void APCGExValencyAssetContainerBase::FindMirroringCages(TArray<APCGExValencyCage*>& OutCages) const
{
	PCGExValencyAssetUtils::FindMirroringCages(this, GetWorld(), OutCages);
}

#pragma endregion

#pragma region Scanning Helpers

void APCGExValencyAssetContainerBase::AddScannedEntry(
	const TSoftObjectPtr<UObject>& Asset,
	AActor* SourceActor,
	const FPCGExValencyMaterialVariant* InMaterialVariant)
{
	if (Asset.IsNull())
	{
		return;
	}

	FPCGExValencyAssetEntry NewEntry;
	NewEntry.Asset = Asset;
	NewEntry.SourceActor = SourceActor;
	NewEntry.AssetType = PCGExValencyAssetUtils::DetectAssetType(Asset);

	// Store material variant on the entry if provided
	if (InMaterialVariant && InMaterialVariant->Overrides.Num() > 0)
	{
		NewEntry.MaterialVariant = *InMaterialVariant;
		NewEntry.bHasMaterialVariant = true;
	}

	// Compute preserved local transform based on flags
	if (SourceActor)
	{
		NewEntry.LocalTransform = ComputePreservedLocalTransform(SourceActor->GetActorTransform());
	}

	// Mark entry to preserve its local transform if the container has that setting enabled
	NewEntry.bPreserveLocalTransform = bPreserveLocalTransforms;

	// Check for duplicates in scanned entries
	// Material variants are a differentiating factor
	const bool bCompareTransforms = bPreserveLocalTransforms || IsConnectorTransformSensitive();
	for (FPCGExValencyAssetEntry& Existing : ScannedAssetEntries)
	{
		if (Existing.Asset == Asset)
		{
			// If both have material variants, check if they match
			if (Existing.bHasMaterialVariant && NewEntry.bHasMaterialVariant)
			{
				if (Existing.MaterialVariant == NewEntry.MaterialVariant)
				{
					// Same asset, same material variant - check transform
					if (!bCompareTransforms || Existing.LocalTransform.Equals(NewEntry.LocalTransform, 0.1f))
					{
						// Increment discovery count as weight
						Existing.MaterialVariant.DiscoveryCount++;
						return;
					}
				}
				// Different material variants - continue to add as separate entry
			}
			else if (!Existing.bHasMaterialVariant && !NewEntry.bHasMaterialVariant)
			{
				// Both have default materials - check transform
				if (!bCompareTransforms || Existing.LocalTransform.Equals(NewEntry.LocalTransform, 0.1f))
				{
					return;
				}
			}
			// One has material variant, one doesn't - they are different entries, continue
		}
	}

	ScannedAssetEntries.Add(NewEntry);

	// Also record to legacy map for backward compatibility with existing builder code
	if (NewEntry.bHasMaterialVariant)
	{
		RecordMaterialVariant(Asset.ToSoftObjectPath(), NewEntry.MaterialVariant.Overrides);
	}
}

void APCGExValencyAssetContainerBase::ClearScannedAssets()
{
	if (ScannedAssetEntries.Num() > 0)
	{
		ScannedAssetEntries.Empty();
		DiscoveredMaterialVariants.Empty();
		OnAssetRegistrationChanged();
	}
}

void APCGExValencyAssetContainerBase::ExtractMaterialOverrides(
	const UStaticMeshComponent* MeshComponent,
	TArray<FPCGExValencyMaterialOverride>& OutOverrides)
{
	PCGExValencyAssetUtils::ExtractMaterialOverrides(MeshComponent, OutOverrides);
}

void APCGExValencyAssetContainerBase::RecordMaterialVariant(
	const FSoftObjectPath& MeshPath,
	const TArray<FPCGExValencyMaterialOverride>& Overrides)
{
	PCGExValencyAssetUtils::RecordMaterialVariant(MeshPath, Overrides, DiscoveredMaterialVariants);
}

#pragma endregion

#pragma region Registration Changed

void APCGExValencyAssetContainerBase::OnAssetRegistrationChanged()
{
	Modify();
	PCGEX_VALENCY_REDRAW_ALL_VIEWPORT
}

#pragma endregion

#pragma region Dragging

void APCGExValencyAssetContainerBase::CollectDraggableActors(TArray<AActor*>& OutActors) const
{
	for (const FPCGExValencyAssetEntry& Entry : ScannedAssetEntries)
	{
		if (AActor* Actor = Entry.SourceActor.Get())
		{
			OutActors.Add(Actor);
		}
	}
}

#pragma endregion
