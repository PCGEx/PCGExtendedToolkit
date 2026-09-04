// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExGenericCollectionEntry.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif

// Registered by hand (not via PCGEX_REGISTER_COLLECTION_TYPE): there is no typed collection, so
// CollectionClass stays null. Static-init registration is safe inside PCGExCollections (the module
// that flushes the pending queue).
namespace PCGExGenericCollectionEntry
{
	struct FTypeRegistration
	{
		FTypeRegistration()
		{
			using FTypeRegistry = PCGExAssetCollection::FTypeRegistry;
			using FTypeInfo = PCGExAssetCollection::FTypeInfo;
			namespace TypeIds = PCGExAssetCollection::TypeIds;

			FTypeRegistry::AddPendingRegistration([]()
			{
				FTypeInfo Info;
				Info.Id = TypeIds::Generic;
				Info.CollectionClass = nullptr;
				Info.EntryStruct = FPCGExGenericCollectionEntry::StaticStruct();
				Info.DisplayName = NSLOCTEXT("PCGEx", "GenericCollectionEntry", "Generic Entry");
				Info.ParentType = TypeIds::Base;
				FTypeRegistry::Get().Register(Info);
			});

#if WITH_EDITOR
			// Catch-all for Omni drops: every real detector must sit below this priority.
			FTypeRegistry::AddPendingCustomization(TypeIds::Generic, [](FTypeInfo& Info)
			{
				Info.SourceDetectPriority = 1000;
				Info.DetectSourceAsset = [](const FAssetData&) { return true; };
			});
#endif
		}
	};

	FTypeRegistration GTypeRegistration;
}

#pragma region FPCGExGenericCollectionEntry

bool FPCGExGenericCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!Asset.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
		{
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

// No asset to measure: bounds are the host's default, filled slot or not. Nothing is loaded.
void FPCGExGenericCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Asset.ToSoftObjectPath();

	// Authored staging carries authoritative bounds from an external system; refresh identity only.
	if (!Staging.bAuthored)
	{
		Staging.Bounds = OwningCollection ? OwningCollection->DefaultStagingBounds : FBox(ForceInit);
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
}

void FPCGExGenericCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	Asset = TSoftObjectPtr<UObject>(InPath);
}

#pragma endregion
