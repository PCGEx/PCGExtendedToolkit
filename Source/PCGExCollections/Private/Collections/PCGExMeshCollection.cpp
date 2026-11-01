// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExMeshCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif


#include "PCGExCollectionsSettingsCache.h"
#include "PCGExLog.h"
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
	for (int32 i = 0; i < Overrides.Num(); i++)
	{
		HighestIndex = FMath::Max(HighestIndex, Overrides[i].ResolveSlot(i));
	}
	return HighestIndex;
}

// Mesh MicroCache - Material variant picking

namespace PCGExMeshCollection
{
#if WITH_EDITORONLY_DATA
	// Legacy full SM descriptor -> extras. Shared members the author edited on the SM side and that the ISM side does
	// not match are named once, and only when these settings reach a spline mesh.
	void FoldLegacyStaticMeshDescriptor(const FPCGExStaticMeshComponentDescriptor& Legacy, const FSoftISMComponentDescriptor& Shared, FPCGExStaticMeshDescriptorExtras& OutExtras, const bool bReachesSplineMeshes, const UObject* Host, const FString& What)
	{
		OutExtras.InitFrom(Legacy);

		if (!bReachesSplineMeshes)
		{
			return;
		}

		TArray<FName> Mismatches;
		PCGExComponentDescriptors::FindSharedMismatches(Legacy, Shared, Mismatches);
		if (Mismatches.IsEmpty())
		{
			return;
		}

		// The two body instances never compare identical (different class defaults), so collision is judged on
		// whether it is on, and like every other member only once the SM side was edited away from its default.
		static const FName BodyInstanceName = GET_MEMBER_NAME_CHECKED(FPCGExStaticMeshComponentDescriptor, BodyInstance);
		const FPCGExStaticMeshComponentDescriptor Defaults;
		TArray<FString> Lost;
		for (const FName& Name : Mismatches)
		{
			if (Name == BodyInstanceName)
			{
				const ECollisionEnabled::Type LegacyCollision = Legacy.BodyInstance.GetCollisionEnabled(false);
				if (LegacyCollision != Defaults.BodyInstance.GetCollisionEnabled(false)
					&& LegacyCollision != Shared.BodyInstance.GetCollisionEnabled(false))
				{
					Lost.Add(Name.ToString());
				}
				continue;
			}

			const FProperty* Property = FPCGExStaticMeshComponentDescriptor::StaticStruct()->FindPropertyByName(Name);
			if (Property && !Property->Identical_InContainer(&Legacy, &Defaults))
			{
				Lost.Add(Name.ToString());
			}
		}
		if (Lost.IsEmpty())
		{
			return;
		}

		UE_LOG(LogPCGEx, Warning, TEXT("[%s] %s: SM settings differed from ISM settings on %s. Spline meshes now use the ISM values."),
		       *GetPathNameSafe(Host), *What, *FString::Join(Lost, TEXT(", ")));
	}

	// Global settings only reach spline meshes through the mesh entries that follow them.
	bool AnyEntryFollowsGlobals(const UPCGExAssetCollection* Host, const EPCGExGlobalVariationRule Mode)
	{
		bool bAnyFollows = false;
		Host->ForEachEntry([&bAnyFollows, Mode](const FPCGExAssetCollectionEntry* Entry, int32)
		{
			if (bAnyFollows || !Entry || Entry->bIsSubCollection || !Entry->IsType(PCGExAssetCollection::TypeIds::Mesh))
			{
				return;
			}

			bAnyFollows = Mode == EPCGExGlobalVariationRule::Overrule
				|| static_cast<const FPCGExMeshCollectionEntry*>(Entry)->DescriptorSource == EPCGExEntryVariationMode::Global;
		});
		return bAnyFollows;
	}

	// An entry on the global descriptor used to spawn spline meshes from its LOCAL SM settings; its static-mesh-only
	// members now come from the global extras, so whatever differs there is named once.
	void WarnIgnoredLocalExtras(const FPCGExStaticMeshDescriptorExtras& Local, const FPCGExStaticMeshDescriptorExtras& Global, const UObject* Host, const FString& What)
	{
		TArray<FString> Ignored;
		for (TFieldIterator<FProperty> It(FPCGExStaticMeshDescriptorExtras::StaticStruct()); It; ++It)
		{
			if (!It->Identical_InContainer(&Local, &Global))
			{
				Ignored.Add(It->GetName());
			}
		}
		if (Ignored.IsEmpty())
		{
			return;
		}

		UE_LOG(LogPCGEx, Warning, TEXT("[%s] %s: follows the global descriptor, so its local SM settings (%s) no longer reach spline meshes."),
		       *GetPathNameSafe(Host), *What, *FString::Join(Ignored, TEXT(", ")));
	}
#endif

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

	if (!ISMDescriptor.OverlayMaterial.IsNull())
	{
		OutPaths.Add(ISMDescriptor.OverlayMaterial.ToSoftObjectPath());
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
		TargetComponent->SetMaterial(PCGExMeshCollection::ResolveSingleSlot(SlotIndex), MaterialOverrideVariants[PickIndex].Material.Get());
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
			const FPCGExMaterialOverrideEntry& SEntry = MEntry.Overrides[i];
			TargetComponent->SetMaterial(SEntry.ResolveSlot(i), SEntry.Material.Get());
		}
	}
}

// PickIndex comes from a serialized collection map and can outlive the variants it indexed.
// Only grows OverrideMaterials: slots the variant doesn't write keep the descriptor's own overrides.
void FPCGExMeshCollectionEntry::ApplyMaterials(int32 PickIndex, FPCGSoftISMComponentDescriptor& Descriptor) const
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
		const int32 WriteSlotIndex = PCGExMeshCollection::ResolveSingleSlot(SlotIndex);
		if (Descriptor.OverrideMaterials.Num() <= WriteSlotIndex)
		{
			Descriptor.OverrideMaterials.SetNum(WriteSlotIndex + 1);
		}
		Descriptor.OverrideMaterials[WriteSlotIndex] = MaterialOverrideVariants[PickIndex].Material;
	}
	else if (MaterialVariants == EPCGExMaterialVariantsMode::Multi)
	{
		if (!MaterialOverrideVariantsList.IsValidIndex(PickIndex))
		{
			return;
		}
		const FPCGExMaterialOverrideCollection& SubList = MaterialOverrideVariantsList[PickIndex];

		const int32 HiIndex = SubList.GetHighestIndex();
		if (HiIndex < 0)
		{
			return;
		}

		if (Descriptor.OverrideMaterials.Num() <= HiIndex)
		{
			Descriptor.OverrideMaterials.SetNum(HiIndex + 1);
		}

		for (int32 i = 0; i < SubList.Overrides.Num(); i++)
		{
			const FPCGExMaterialOverrideEntry& SlotEntry = SubList.Overrides[i];
			Descriptor.OverrideMaterials[SlotEntry.ResolveSlot(i)] = SlotEntry.Material;
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
	StagedMaterials.Reset();
	StagedMaterialsSource.Reset();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	if (Staging.InternalIndex == -1 && PCGEX_COLLECTIONS_SETTINGS.bDisableCollisionByDefault)
	{
		ISMDescriptor.BodyInstance.SetCollisionEnabled(ECollisionEnabled::Type::NoCollision);
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

		const TArray<FStaticMaterial>& MeshSlots = M->GetStaticMaterials();
		StagedMaterials.Reserve(MeshSlots.Num());
		for (const FStaticMaterial& Slot : MeshSlots)
		{
			StagedMaterials.Emplace(Slot.MaterialInterface.Get());
		}
		StagedMaterialsSource = Staging.Path;
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
	StagedMaterials.Reset();
	StagedMaterialsSource.Reset();
}

void FPCGExMeshCollectionEntry::ResolveGlobalsToLocal(const UPCGExAssetCollection* InSourceCollection)
{
	if (!InSourceCollection)
	{
		return;
	}

	FPCGExMeshCollectionGlobals Globals;
	if (!InSourceCollection->GetTypeGlobals(Globals))
	{
		return;
	}

	// Mirror the effective-descriptor rule from InitPCGSoftISMDescriptor: Global source or
	// collection-level Overrule means the collection's globals were what actually applied.
	if (DescriptorSource == EPCGExEntryVariationMode::Global || Globals.GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule)
	{
		ISMDescriptor = Globals.GlobalISMDescriptor;
		SMExtras = Globals.GlobalSMExtras;
		ISMDescriptor.StaticMesh = StaticMesh; // globals don't carry the entry's asset
		DescriptorSource = EPCGExEntryVariationMode::Local;
	}
}

// Resolves descriptor inheritance: Global/Overrule → collection-level descriptor (via the
// seam), Local → entry-level ISMDescriptor. Always appends entry tags to component tags.
void FPCGExMeshCollectionEntry::InitPCGSoftISMDescriptor(const UPCGExAssetCollection* ParentCollection, FPCGSoftISMComponentDescriptor& TargetDescriptor) const
{
	FPCGExMeshCollectionGlobals Globals;
	const bool bHasGlobals = ParentCollection && ParentCollection->GetTypeGlobals(Globals);

	if (bHasGlobals && (DescriptorSource == EPCGExEntryVariationMode::Global || Globals.GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule))
	{
		PCGExPropertyHelpers::CopyStructProperties(&Globals.GlobalISMDescriptor, &TargetDescriptor, FSoftISMComponentDescriptor::StaticStruct(), FPCGSoftISMComponentDescriptor::StaticStruct());
		TargetDescriptor.ComponentTags.Append(ParentCollection->CollectionTags.Array());
	}
	else
	{
		PCGExPropertyHelpers::CopyStructProperties(&ISMDescriptor, &TargetDescriptor, FSoftISMComponentDescriptor::StaticStruct(), FPCGSoftISMComponentDescriptor::StaticStruct());
	}

	// The entry's asset is authoritative: globals don't carry one, and a local descriptor only
	// mirrors it through SetAssetPath/EDITOR_Sanitize -- which programmatic authoring bypasses.
	TargetDescriptor.StaticMesh = StaticMesh;
	TargetDescriptor.ComponentTags.Append(Tags.Array());
}

void FPCGExMeshCollectionEntry::ComposeStaticMeshDescriptor(const UPCGExAssetCollection* ParentCollection, FPCGExStaticMeshComponentDescriptor& OutDescriptor) const
{
	FPCGExMeshCollectionGlobals Globals;
	const bool bHasGlobals = ParentCollection && ParentCollection->GetTypeGlobals(Globals);

	if (bHasGlobals && (DescriptorSource == EPCGExEntryVariationMode::Global || Globals.GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule))
	{
		PCGExComponentDescriptors::Compose(Globals.GlobalISMDescriptor, Globals.GlobalSMExtras, OutDescriptor);
	}
	else
	{
		PCGExComponentDescriptors::Compose(ISMDescriptor, SMExtras, OutDescriptor);
	}
}

bool FPCGExMeshCollectionEntry::OnHostPostLoad(UPCGExAssetCollection* Host)
{
	bool bRewritten = FPCGExAssetCollectionEntry::OnHostPostLoad(Host);

#if WITH_EDITORONLY_DATA
	if (Host && Host->GetLoadedCollectionsVersion() < FPCGExCollectionsCustomVersion::MeshEntryStaticMeshExtras)
	{
		const FString What = FString::Printf(TEXT("entry '%s'"), *StaticMesh.ToSoftObjectPath().ToString());
		PCGExMeshCollection::FoldLegacyStaticMeshDescriptor(SMDescriptor_DEPRECATED, ISMDescriptor, SMExtras, !bIsSubCollection, Host, What);

		// Host globals are folded before entries run (see UPCGExMeshCollection::PostLoad), so this compares migrated data.
		FPCGExMeshCollectionGlobals Globals;
		if (!bIsSubCollection && Host->GetTypeGlobals(Globals)
			&& (DescriptorSource == EPCGExEntryVariationMode::Global || Globals.GlobalDescriptorMode == EPCGExGlobalVariationRule::Overrule))
		{
			PCGExMeshCollection::WarnIgnoredLocalExtras(SMExtras, Globals.GlobalSMExtras, Host, What);
		}
		bRewritten = true;
	}
#endif

	return bRewritten;
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
		NewCache->ProcessMaterialOverrides(MaterialOverrideVariants, PCGExMeshCollection::ResolveSingleSlot(SlotIndex));
		break;
	case EPCGExMaterialVariantsMode::Multi:
		NewCache->ProcessMaterialOverrides(MaterialOverrideVariantsList);
		break;
	}

	MicroCache = NewCache;
}

bool FPCGExMeshCollectionEntry::Serialize(FArchive& Ar)
{
	return PCGExAssetCollection::SerializeEntryDelta(*this, Ar);
}

bool FPCGExMeshCollectionGlobals::OnHostPostLoad(UPCGExAssetCollection* Host)
{
#if WITH_EDITORONLY_DATA
	if (Host && Host->GetLoadedCollectionsVersion() < FPCGExCollectionsCustomVersion::MeshEntryStaticMeshExtras)
	{
		PCGExMeshCollection::FoldLegacyStaticMeshDescriptor(GlobalSMDescriptor_DEPRECATED, GlobalISMDescriptor, GlobalSMExtras, PCGExMeshCollection::AnyEntryFollowsGlobals(Host, GlobalDescriptorMode), Host, TEXT("global SM settings"));
		return true;
	}
#endif
	return false;
}

bool UPCGExMeshCollection::GetTypeGlobalsInternal(const UScriptStruct* StructType, FPCGExCollectionTypeGlobals& OutGlobals) const
{
	if (!StructType || !StructType->IsChildOf(FPCGExMeshCollectionGlobals::StaticStruct()))
	{
		return Super::GetTypeGlobalsInternal(StructType, OutGlobals);
	}

	FPCGExMeshCollectionGlobals& Out = static_cast<FPCGExMeshCollectionGlobals&>(OutGlobals);
	Out.GlobalDescriptorMode = GlobalDescriptorMode;
	Out.GlobalISMDescriptor = GlobalISMDescriptor;
	Out.GlobalSMExtras = GlobalSMExtras;
	return true;
}

void UPCGExMeshCollection::PostLoad()
{
#if WITH_EDITORONLY_DATA
	// Before Super: the entry hooks it runs read the globals through the seam and must see them folded.
	if (GetLoadedCollectionsVersion() < FPCGExCollectionsCustomVersion::MeshEntryStaticMeshExtras)
	{
		PCGExMeshCollection::FoldLegacyStaticMeshDescriptor(GlobalSMDescriptor_DEPRECATED, GlobalISMDescriptor, GlobalSMExtras, PCGExMeshCollection::AnyEntryFollowsGlobals(this, GlobalDescriptorMode), this, TEXT("global SM settings"));
#if WITH_EDITOR
		(void)MarkPackageDirty();
#endif
	}
#endif

	Super::PostLoad();
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
