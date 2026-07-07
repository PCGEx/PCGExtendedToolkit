// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExMeshCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif


#include "PCGExCollectionsSettingsCache.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshSocket.h"
#include "Helpers/PCGExPropertyHelpers.h"
#include "Materials/MaterialInterface.h"

// Static-init type registration: TypeId=Mesh, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(Mesh, UPCGExMeshCollection, FPCGExMeshCollectionEntry, "Mesh Collection", Base)

// Material Override Collection

void FPCGExMaterialOverrideCollection::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	for (const FPCGExMaterialOverrideEntry& Entry : Overrides)
	{
		OutPaths.Add(Entry.Material.ToSoftObjectPath());
	}
}

int32 FPCGExMaterialOverrideCollection::GetHighestIndex() const
{
	int32 HighestIndex = -1;
	for (const FPCGExMaterialOverrideEntry& Entry : Overrides)
	{
		HighestIndex = FMath::Max(HighestIndex, Entry.SlotIndex);
	}
	return HighestIndex;
}

// Mesh MicroCache - Material variant picking

namespace PCGExMeshCollection
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

// Mesh Collection Entry

void FPCGExMeshCollectionEntry::GetAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
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

	// ISM materials
	for (int32 i = 0; i < ISMDescriptor.OverrideMaterials.Num(); i++)
	{
		if (!ISMDescriptor.OverrideMaterials[i].IsNull())
		{
			OutPaths.Add(ISMDescriptor.OverrideMaterials[i].ToSoftObjectPath());
		}
	}

	for (int32 i = 0; i < ISMDescriptor.RuntimeVirtualTextures.Num(); i++)
	{
		if (!ISMDescriptor.RuntimeVirtualTextures[i].IsNull())
		{
			OutPaths.Add(ISMDescriptor.RuntimeVirtualTextures[i].ToSoftObjectPath());
		}
	}

	// SM materials
	for (int32 i = 0; i < SMDescriptor.OverrideMaterials.Num(); i++)
	{
		if (!SMDescriptor.OverrideMaterials[i].IsNull())
		{
			OutPaths.Add(SMDescriptor.OverrideMaterials[i].ToSoftObjectPath());
		}
	}

	for (int32 i = 0; i < SMDescriptor.RuntimeVirtualTextures.Num(); i++)
	{
		if (!SMDescriptor.RuntimeVirtualTextures[i].IsNull())
		{
			OutPaths.Add(SMDescriptor.RuntimeVirtualTextures[i].ToSoftObjectPath());
		}
	}
}

void FPCGExMeshCollectionEntry::GetMaterialPaths(int32 PickIndex, TSet<FSoftObjectPath>& OutPaths) const
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

void FPCGExMeshCollectionEntry::ApplyMaterials(int32 PickIndex, UStaticMeshComponent* TargetComponent) const
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
		const int32 WriteSlotIndex = SlotIndex == -1 ? 0 : SlotIndex;
		TargetComponent->SetMaterial(WriteSlotIndex, MaterialOverrideVariants[PickIndex].Material.Get());
	}
	else if (MaterialVariants == EPCGExMaterialVariantsMode::Multi)
	{
		if (!MaterialOverrideVariantsList.IsValidIndex(PickIndex))
		{
			return;
		}
		const FPCGExMaterialOverrideCollection& MEntry = MaterialOverrideVariantsList[PickIndex];

		for (const FPCGExMaterialOverrideEntry& SEntry : MEntry.Overrides)
		{
			const int32 WriteSlotIndex = SEntry.SlotIndex == -1 ? 0 : SEntry.SlotIndex;
			TargetComponent->SetMaterial(WriteSlotIndex, SEntry.Material.Get());
		}
	}
}

void FPCGExMeshCollectionEntry::ApplyMaterials(int32 PickIndex, FPCGSoftISMComponentDescriptor& Descriptor) const
{
	if (PickIndex == -1 || MaterialVariants == EPCGExMaterialVariantsMode::None)
	{
		return;
	}

	if (MaterialVariants == EPCGExMaterialVariantsMode::Single)
	{
		const int32 WriteSlotIndex = SlotIndex == -1 ? 0 : SlotIndex;
		Descriptor.OverrideMaterials.SetNum(WriteSlotIndex + 1);
		Descriptor.OverrideMaterials[WriteSlotIndex] = MaterialOverrideVariants[PickIndex].Material;
	}
	else if (MaterialVariants == EPCGExMaterialVariantsMode::Multi)
	{
		const FPCGExMaterialOverrideCollection& SubList = MaterialOverrideVariantsList[PickIndex];

		const int32 HiIndex = SubList.GetHighestIndex();
		Descriptor.OverrideMaterials.SetNum(HiIndex == -1 ? 1 : HiIndex + 1);

		for (int32 i = 0; i < SubList.Overrides.Num(); i++)
		{
			const FPCGExMaterialOverrideEntry& SlotEntry = SubList.Overrides[i];
			Descriptor.OverrideMaterials[SlotEntry.SlotIndex == -1 ? 0 : SlotEntry.SlotIndex] = SlotEntry.Material;
		}
	}
}

bool FPCGExMeshCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!StaticMesh.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
		{
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

// Loads the static mesh to extract bounds and sockets into Staging.
// First-time entries (InternalIndex == -1) get collision disabled if configured.
void FPCGExMeshCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	if (Staging.InternalIndex == -1 && PCGEX_COLLECTIONS_SETTINGS.bDisableCollisionByDefault)
	{
		ISMDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
		SMDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	}

	Staging.Path = StaticMesh.ToSoftObjectPath();

	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThreadTpl(StaticMesh);

	if (const UStaticMesh* M = StaticMesh.Get())
	{
		Staging.Bounds = M->GetBoundingBox();
		for (const TObjectPtr<UStaticMeshSocket>& MSocket : M->Sockets)
		{
			FPCGExSocket& NewSocket = Staging.Sockets.Emplace_GetRef(
				MSocket->SocketName, MSocket->RelativeLocation, MSocket->RelativeRotation, MSocket->RelativeScale, MSocket->Tag);
			NewSocket.bManaged = true;
		}
	}
	else
	{
		Staging.Bounds = FBox(ForceInit);
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExMeshCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	StaticMesh = TSoftObjectPtr<UStaticMesh>(InPath);
	ISMDescriptor.StaticMesh = StaticMesh;
}

void FPCGExMeshCollectionEntry::ResolveGlobalsToLocal(const UPCGExAssetCollection* InSourceCollection)
{
	const UPCGExMeshCollection* TypedCollection = Cast<UPCGExMeshCollection>(InSourceCollection);
	if (!TypedCollection)
	{
		return;
	}

	// Mirror the effective-descriptor rule from InitPCGSoftISMDescriptor: Global source or
	// collection-level Overrule means the collection's globals were what actually applied.
	if (DescriptorSource == EPCGExEntryVariationMode::Global || TypedCollection->GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule)
	{
		ISMDescriptor = TypedCollection->GlobalISMDescriptor;
		SMDescriptor = TypedCollection->GlobalSMDescriptor;
		ISMDescriptor.StaticMesh = StaticMesh; // globals don't carry the entry's asset
		DescriptorSource = EPCGExEntryVariationMode::Local;
	}
}

// Resolves descriptor inheritance: Global/Overrule → use collection-level descriptor,
// Local → use entry-level ISMDescriptor. Always appends entry tags to component tags.
void FPCGExMeshCollectionEntry::InitPCGSoftISMDescriptor(const UPCGExMeshCollection* ParentCollection, FPCGSoftISMComponentDescriptor& TargetDescriptor) const
{
	if (ParentCollection && (DescriptorSource == EPCGExEntryVariationMode::Global || ParentCollection->GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule))
	{
		PCGExPropertyHelpers::CopyStructProperties(&ParentCollection->GlobalISMDescriptor, &TargetDescriptor, FSoftISMComponentDescriptor::StaticStruct(), FPCGSoftISMComponentDescriptor::StaticStruct());

		TargetDescriptor.StaticMesh = StaticMesh;
		TargetDescriptor.ComponentTags.Append(ParentCollection->CollectionTags.Array());
	}
	else
	{
		PCGExPropertyHelpers::CopyStructProperties(&ISMDescriptor, &TargetDescriptor, FSoftISMComponentDescriptor::StaticStruct(), FPCGSoftISMComponentDescriptor::StaticStruct());
	}

	TargetDescriptor.ComponentTags.Append(Tags.Array());
}

#if WITH_EDITOR
void FPCGExMeshCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	if (!bIsSubCollection && StaticMesh)
	{
		ISMDescriptor.StaticMesh = StaticMesh;
	}
}
#endif

void FPCGExMeshCollectionEntry::BuildMicroCache()
{
	TSharedPtr<PCGExMeshCollection::FMicroCache> NewCache = MakeShared<PCGExMeshCollection::FMicroCache>();

	switch (MaterialVariants)
	{
	default:
	case EPCGExMaterialVariantsMode::None:
		break;
	case EPCGExMaterialVariantsMode::Single:
		NewCache->ProcessMaterialOverrides(MaterialOverrideVariants, SlotIndex);
		break;
	case EPCGExMaterialVariantsMode::Multi:
		NewCache->ProcessMaterialOverrides(MaterialOverrideVariantsList);
		break;
	}

	MicroCache = NewCache;
}


#if WITH_EDITOR

// Mesh Collection - Editor Functions

void UPCGExMeshCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		TSoftObjectPtr<UStaticMesh> Mesh = TSoftObjectPtr<UStaticMesh>(SelectedAsset.ToSoftObjectPath());
		if (!Mesh.LoadSynchronous())
		{
			continue;
		}

		bool bAlreadyExists = false;
		for (const FPCGExMeshCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.StaticMesh == Mesh)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}

		FPCGExMeshCollectionEntry Entry = FPCGExMeshCollectionEntry();
		Entry.StaticMesh = Mesh;

		Entries.Add(Entry);
	}
}

void UPCGExMeshCollection::EDITOR_DisableCollisions()
{
	Modify(true);

	for (FPCGExMeshCollectionEntry& Entry : Entries)
	{
		Entry.ISMDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
		Entry.SMDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
	}

	FPropertyChangedEvent EmptyEvent(nullptr);
	PostEditChangeProperty(EmptyEvent);
	MarkPackageDirty();
}

void UPCGExMeshCollection::EDITOR_SetDescriptorSourceAll(EPCGExEntryVariationMode DescriptorSource)
{
	// None is a variations/fitting-source concept; descriptors always carry data. Clamp to Local.
	if (DescriptorSource == EPCGExEntryVariationMode::None)
	{
		DescriptorSource = EPCGExEntryVariationMode::Local;
	}

	Modify(true);

	for (FPCGExMeshCollectionEntry& Entry : Entries)
	{
		Entry.DescriptorSource = DescriptorSource;
	}

	FPropertyChangedEvent EmptyEvent(nullptr);
	PostEditChangeProperty(EmptyEvent);
	MarkPackageDirty();
}
#endif
