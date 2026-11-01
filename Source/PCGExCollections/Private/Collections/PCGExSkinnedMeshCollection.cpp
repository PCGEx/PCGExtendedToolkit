// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExSkinnedMeshCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif


#include "PCGExCollectionsSettingsCache.h"
#include "Components/MeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Helpers/PCGExPropertyHelpers.h"
#include "Materials/MaterialInterface.h"

// Static-init type registration: TypeId=SkinnedMesh, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(SkinnedMesh, UPCGExSkinnedMeshCollection, FPCGExSkinnedMeshCollectionEntry, "Skinned Mesh Collection", Base)

// Skinned Mesh MicroCache - Material variant picking

namespace PCGExSkinnedMeshCollection
{
	void FMicroCache::ProcessMaterialOverrides(const TArray<FPCGExMaterialOverrideSingleEntry>& Overrides, int32 InSlotIndex)
	{
		HighestMaterialIndex = InSlotIndex;

		TArray<int32> WeightArray;
		WeightArray.SetNumUninitialized(Overrides.Num());
		for (int32 i = 0; i < Overrides.Num(); i++)
		{
			WeightArray[i] = Overrides[i].Weight;
		}

		BuildFromWeights(WeightArray);
	}

	void FMicroCache::ProcessMaterialOverrides(const TArray<FPCGExMaterialOverrideCollection>& Overrides)
	{
		HighestMaterialIndex = -1;

		TArray<int32> WeightArray;
		WeightArray.SetNumUninitialized(Overrides.Num());
		for (int32 i = 0; i < Overrides.Num(); i++)
		{
			WeightArray[i] = Overrides[i].Weight;
			HighestMaterialIndex = FMath::Max(HighestMaterialIndex, Overrides[i].GetHighestIndex());
		}

		BuildFromWeights(WeightArray);
	}
}

// Skinned Mesh Collection Entry

void FPCGExSkinnedMeshCollectionEntry::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	FPCGExAssetCollectionEntry::GetAssetPaths(OutPaths);

	// Material overrides
	switch (MaterialVariants)
	{
	default:
	case EPCGExMaterialVariantsMode::None:
		break;
	case EPCGExMaterialVariantsMode::Single:
		for (const FPCGExMaterialOverrideSingleEntry& Entry : MaterialOverrideVariants)
		{
			OutPaths.Add(Entry.Material.ToSoftObjectPath());
		}
		break;
	case EPCGExMaterialVariantsMode::Multi:
		for (const FPCGExMaterialOverrideCollection& Entry : MaterialOverrideVariantsList)
		{
			Entry.GetAssetPaths(OutPaths);
		}
		break;
	}

	// The local descriptor's provider only: a global descriptor's belongs to the collection asset.
	if (!Descriptor.TransformProvider.IsNull())
	{
		OutPaths.Add(Descriptor.TransformProvider.ToSoftObjectPath());
	}
}

void FPCGExSkinnedMeshCollectionEntry::GetMaterialPaths(int32 PickIndex, TSet<FSoftObjectPath>& OutPaths) const
{
	if (PickIndex == -1 || MaterialVariants == EPCGExMaterialVariantsMode::None)
	{
		return;
	}

	if (MaterialVariants == EPCGExMaterialVariantsMode::Single)
	{
		if (!MaterialOverrideVariants.IsValidIndex(PickIndex))
		{
			return;
		}
		OutPaths.Add(MaterialOverrideVariants[PickIndex].Material.ToSoftObjectPath());
	}
	else if (MaterialVariants == EPCGExMaterialVariantsMode::Multi)
	{
		if (!MaterialOverrideVariantsList.IsValidIndex(PickIndex))
		{
			return;
		}
		const FPCGExMaterialOverrideCollection& MEntry = MaterialOverrideVariantsList[PickIndex];

		for (int32 i = 0; i < MEntry.Overrides.Num(); i++)
		{
			OutPaths.Add(MEntry.Overrides[i].Material.ToSoftObjectPath());
		}
	}
}

// PickIndex comes from a serialized collection map and can outlive the variants it indexed.
void FPCGExSkinnedMeshCollectionEntry::ApplyMaterials(int32 PickIndex, UMeshComponent* TargetComponent) const
{
	if (PickIndex == -1 || MaterialVariants == EPCGExMaterialVariantsMode::None)
	{
		return;
	}

	if (MaterialVariants == EPCGExMaterialVariantsMode::Single)
	{
		if (!MaterialOverrideVariants.IsValidIndex(PickIndex))
		{
			return;
		}
		TargetComponent->SetMaterial(PCGExMeshCollection::ResolveSingleSlot(SlotIndex), MaterialOverrideVariants[PickIndex].Material.Get());
	}
	else if (MaterialVariants == EPCGExMaterialVariantsMode::Multi)
	{
		if (!MaterialOverrideVariantsList.IsValidIndex(PickIndex))
		{
			return;
		}
		const TArray<FPCGExMaterialOverrideEntry>& SlotEntries = MaterialOverrideVariantsList[PickIndex].Overrides;
		for (int32 i = 0; i < SlotEntries.Num(); i++)
		{
			TargetComponent->SetMaterial(SlotEntries[i].ResolveSlot(i), SlotEntries[i].Material.Get());
		}
	}
}

bool FPCGExSkinnedMeshCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!SkinnedAsset.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
		{
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

// Loads the skinned asset to extract bounds and sockets into Staging.
// First-time entries (InternalIndex == -1) get collision disabled if configured.
// Sockets come from USkeletalMesh::GetActiveSocketList() when the underlying asset is a skeletal mesh.
void FPCGExSkinnedMeshCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	if (Staging.InternalIndex == -1 && PCGEX_COLLECTIONS_SETTINGS.bDisableCollisionByDefault)
	{
		Descriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	}

	Staging.Path = SkinnedAsset.ToSoftObjectPath();

	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThreadTpl(SkinnedAsset);

	if (const USkinnedAsset* Asset = SkinnedAsset.Get())
	{
		Staging.Bounds = Asset->GetBounds().GetBox();

		if (const USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset))
		{
			for (const USkeletalMeshSocket* MSocket : SkelMesh->GetActiveSocketList())
			{
				if (!MSocket)
				{
					continue;
				}
				FPCGExSocket& NewSocket = Staging.Sockets.Emplace_GetRef(
					MSocket->SocketName, MSocket->RelativeLocation, MSocket->RelativeRotation, MSocket->RelativeScale, MSocket->BoneName.ToString());
				NewSocket.bManaged = true;
			}
		}
	}
	else
	{
		Staging.Bounds = FBox(ForceInit);
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExSkinnedMeshCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	SkinnedAsset = TSoftObjectPtr<USkinnedAsset>(InPath);
	Descriptor.SkinnedAsset = SkinnedAsset;
}

void FPCGExSkinnedMeshCollectionEntry::ResolveGlobalsToLocal(const UPCGExAssetCollection* InSourceCollection)
{
	if (!InSourceCollection)
	{
		return;
	}

	FPCGExSkinnedMeshCollectionGlobals Globals;
	if (!InSourceCollection->GetTypeGlobals(Globals))
	{
		return;
	}

	// Mirror the effective-descriptor rule from InitPCGSoftSkinnedDescriptor.
	if (DescriptorSource == EPCGExEntryVariationMode::Global || Globals.GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule)
	{
		Descriptor = Globals.GlobalDescriptor;
		Descriptor.SkinnedAsset = SkinnedAsset; // globals don't carry the entry's asset
		DescriptorSource = EPCGExEntryVariationMode::Local;
	}
}

// Resolves descriptor inheritance: Global/Overrule → collection-level descriptor (via the
// seam), Local → entry-level Descriptor. Always appends entry tags to component tags.
void FPCGExSkinnedMeshCollectionEntry::InitPCGSoftSkinnedDescriptor(const UPCGExAssetCollection* ParentCollection, FPCGSoftSkinnedMeshComponentDescriptor& TargetDescriptor) const
{
	FPCGExSkinnedMeshCollectionGlobals Globals;
	const bool bHasGlobals = ParentCollection && ParentCollection->GetTypeGlobals(Globals);

	if (bHasGlobals && (DescriptorSource == EPCGExEntryVariationMode::Global || Globals.GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule))
	{
		PCGExPropertyHelpers::CopyStructProperties(&Globals.GlobalDescriptor, &TargetDescriptor, FPCGSoftSkinnedMeshComponentDescriptor::StaticStruct(), FPCGSoftSkinnedMeshComponentDescriptor::StaticStruct());
		TargetDescriptor.ComponentTags.Append(ParentCollection->CollectionTags.Array());
	}
	else
	{
		PCGExPropertyHelpers::CopyStructProperties(&Descriptor, &TargetDescriptor, FPCGSoftSkinnedMeshComponentDescriptor::StaticStruct(), FPCGSoftSkinnedMeshComponentDescriptor::StaticStruct());
	}

	// The entry's asset is authoritative: globals don't carry one, and a local descriptor only
	// mirrors it through SetAssetPath/EDITOR_Sanitize -- which programmatic authoring bypasses.
	TargetDescriptor.SkinnedAsset = SkinnedAsset;
	TargetDescriptor.ComponentTags.Append(Tags.Array());
}

#if WITH_EDITOR
void FPCGExSkinnedMeshCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	if (!bIsSubCollection && SkinnedAsset)
	{
		Descriptor.SkinnedAsset = SkinnedAsset;
	}
}
#endif

void FPCGExSkinnedMeshCollectionEntry::BuildMicroCache()
{
	TSharedPtr<PCGExSkinnedMeshCollection::FMicroCache> NewCache = MakeShared<PCGExSkinnedMeshCollection::FMicroCache>();

	switch (MaterialVariants)
	{
	default:
	case EPCGExMaterialVariantsMode::None:
		break;
	case EPCGExMaterialVariantsMode::Single:
		NewCache->ProcessMaterialOverrides(MaterialOverrideVariants, PCGExMeshCollection::ResolveSingleSlot(SlotIndex));
		break;
	case EPCGExMaterialVariantsMode::Multi:
		NewCache->ProcessMaterialOverrides(MaterialOverrideVariantsList);
		break;
	}

	MicroCache = NewCache;
}

bool FPCGExSkinnedMeshCollectionEntry::Serialize(FArchive& Ar)
{
	return PCGExAssetCollection::SerializeEntryDelta(*this, Ar);
}

bool UPCGExSkinnedMeshCollection::GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const
{
	if (!StructType || !StructType->IsChildOf(FPCGExSkinnedMeshCollectionGlobals::StaticStruct()))
	{
		return Super::GetTypeGlobalsInternal(StructType, OutGlobals);
	}

	FPCGExSkinnedMeshCollectionGlobals& Out = static_cast<FPCGExSkinnedMeshCollectionGlobals&>(OutGlobals);
	Out.GlobalDescriptorMode = GlobalDescriptorMode;
	Out.GlobalDescriptor = GlobalDescriptor;
	return true;
}


#if WITH_EDITOR

// Skinned Mesh Collection - Editor Functions

void UPCGExSkinnedMeshCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		TSoftObjectPtr<USkinnedAsset> Asset = TSoftObjectPtr<USkinnedAsset>(SelectedAsset.ToSoftObjectPath());
		if (!Asset.LoadSynchronous())
		{
			continue;
		}

		bool bAlreadyExists = false;
		for (const FPCGExSkinnedMeshCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.SkinnedAsset == Asset)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}

		FPCGExSkinnedMeshCollectionEntry Entry = FPCGExSkinnedMeshCollectionEntry();
		Entry.SkinnedAsset = Asset;

		Entries.Add(Entry);
	}
}

void UPCGExSkinnedMeshCollection::EDITOR_DisableCollisions()
{
	Modify(true);

	for (FPCGExSkinnedMeshCollectionEntry& Entry : Entries)
	{
		Entry.Descriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	}

	FPropertyChangedEvent EmptyEvent(nullptr);
	PostEditChangeProperty(EmptyEvent);
	MarkPackageDirty();
}

void UPCGExSkinnedMeshCollection::EDITOR_SetDescriptorSourceAll(EPCGExEntryVariationMode DescriptorSource)
{
	Modify(true);

	for (FPCGExSkinnedMeshCollectionEntry& Entry : Entries)
	{
		Entry.DescriptorSource = DescriptorSource;
	}

	FPropertyChangedEvent EmptyEvent(nullptr);
	PostEditChangeProperty(EmptyEvent);
	MarkPackageDirty();
}
#endif
