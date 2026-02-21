// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExPCGDataAssetCollection.h"

#include "Engine/World.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif

#include "PCGDataAsset.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGExLevelDataExporter.h"
#include "Helpers/PCGExDefaultLevelDataExporter.h"
#include "PCGExLog.h"


// Static-init type registration: TypeId=PCGDataAsset, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(PCGDataAsset, UPCGExPCGDataAssetCollection, FPCGExPCGDataAssetCollectionEntry, "PCG Data Asset Collection", Base)

// PCGDataAsset MicroCache - Point weight picking

namespace PCGExPCGDataAssetCollection
{
	void FMicroCache::ProcessPointWeights(const TArray<int32>& InPointWeights)
	{
		BuildFromWeights(InPointWeights);
	}
}

// PCGDataAsset Collection Entry

UPCGExAssetCollection* FPCGExPCGDataAssetCollectionEntry::GetSubCollectionPtr() const
{
	return SubCollection;
}

void FPCGExPCGDataAssetCollectionEntry::ClearSubCollection()
{
	FPCGExAssetCollectionEntry::ClearSubCollection();
	SubCollection = nullptr;
}

bool FPCGExPCGDataAssetCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (Source == EPCGExDataAssetEntrySource::Level)
		{
			if (!Level.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries) { return false; }
		}
		else
		{
			if (!DataAsset.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries) { return false; }
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

namespace PCGExPCGDataAssetCollectionInternal
{
	/** Compute combined bounds from all spatial data in a PCGDataAsset. */
	static FBox ComputeBoundsFromAsset(const UPCGDataAsset* Asset)
	{
		FBox CombinedBounds(ForceInit);
		if (Asset)
		{
			for (const FPCGTaggedData& TaggedData : Asset->Data.GetAllInputs())
			{
				if (const UPCGSpatialData* SpatialData = Cast<UPCGSpatialData>(TaggedData.Data))
				{
					CombinedBounds += SpatialData->GetBounds();
				}
			}
		}
		return CombinedBounds.IsValid ? CombinedBounds : FBox(ForceInit);
	}
}

// Loads the PCG data asset (or exports level data) and computes combined bounds.
void FPCGExPCGDataAssetCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	if (Source == EPCGExDataAssetEntrySource::Level)
	{
		// Level source: load world, export to embedded data asset
		TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(Level.ToSoftObjectPath());
		UWorld* LoadedWorld = Level.Get();

		if (!LoadedWorld)
		{
			Staging.Bounds = FBox(ForceInit);
			Staging.Path = FSoftObjectPath();
			PCGExHelpers::SafeReleaseHandle(Handle);
			FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
			return;
		}

		// Create or reuse embedded data asset, outered to the owning collection
		if (!ExportedDataAsset || ExportedDataAsset->GetOuter() != OwningCollection)
		{
			ExportedDataAsset = NewObject<UPCGDataAsset>(const_cast<UPCGExAssetCollection*>(OwningCollection));
		}
		ExportedDataAsset->Data.TaggedData.Reset();

		// Use collection's instanced exporter if available, otherwise create a transient default
		UPCGExLevelDataExporter* Exporter = nullptr;
		if (const UPCGExPCGDataAssetCollection* TypedCollection = Cast<UPCGExPCGDataAssetCollection>(OwningCollection))
		{
			Exporter = TypedCollection->LevelExporter;
		}

		TObjectPtr<UPCGExDefaultLevelDataExporter> FallbackExporter;
		if (!Exporter)
		{
			FallbackExporter = NewObject<UPCGExDefaultLevelDataExporter>(GetTransientPackage());
			Exporter = FallbackExporter;
		}

		// Run export
		const bool bSuccess = Exporter->ExportLevelData(LoadedWorld, ExportedDataAsset);

		if (bSuccess)
		{
			Staging.Path = FSoftObjectPath(ExportedDataAsset);
			Staging.Bounds = PCGExPCGDataAssetCollectionInternal::ComputeBoundsFromAsset(ExportedDataAsset);
		}
		else
		{
			Staging.Path = FSoftObjectPath();
			Staging.Bounds = FBox(ForceInit);
		}

		PCGExHelpers::SafeReleaseHandle(Handle);
	}
	else
	{
		// DataAsset source: existing behavior
		Staging.Path = DataAsset.ToSoftObjectPath();
		TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThreadTpl(DataAsset);

		if (const UPCGDataAsset* Asset = DataAsset.Get())
		{
			Staging.Bounds = PCGExPCGDataAssetCollectionInternal::ComputeBoundsFromAsset(Asset);
		}
		else
		{
			Staging.Bounds = FBox(ForceInit);
		}

		PCGExHelpers::SafeReleaseHandle(Handle);
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
}

void FPCGExPCGDataAssetCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);

	if (Source == EPCGExDataAssetEntrySource::Level)
	{
		Level = TSoftObjectPtr<UWorld>(InPath);
	}
	else
	{
		DataAsset = TSoftObjectPtr<UPCGDataAsset>(InPath);
	}
}

#if WITH_EDITOR
void FPCGExPCGDataAssetCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	if (!bIsSubCollection)
	{
		InternalSubCollection = nullptr;
	}
	else
	{
		InternalSubCollection = SubCollection;
	}

	// Clean up embedded data asset when not in Level mode
	if (Source != EPCGExDataAssetEntrySource::Level)
	{
		ExportedDataAsset = nullptr;
	}
}
#endif

void FPCGExPCGDataAssetCollectionEntry::BuildMicroCache()
{
	if (!bOverrideWeights || PointWeights.IsEmpty())
	{
		MicroCache = nullptr;
		return;
	}

	TSharedPtr<PCGExPCGDataAssetCollection::FMicroCache> NewCache = MakeShared<PCGExPCGDataAssetCollection::FMicroCache>();
	NewCache->ProcessPointWeights(PointWeights);
	MicroCache = NewCache;
}


#if WITH_EDITOR

// PCGDataAsset Collection - Editor Functions

void UPCGExPCGDataAssetCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		// Try as UWorld (Level source)
		if (SelectedAsset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
		{
			TSoftObjectPtr<UWorld> WorldAsset(SelectedAsset.GetSoftObjectPath());

			bool bAlreadyExists = false;
			for (const FPCGExPCGDataAssetCollectionEntry& ExistingEntry : Entries)
			{
				if (ExistingEntry.Source == EPCGExDataAssetEntrySource::Level && ExistingEntry.Level == WorldAsset)
				{
					bAlreadyExists = true;
					break;
				}
			}

			if (bAlreadyExists) { continue; }

			FPCGExPCGDataAssetCollectionEntry Entry;
			Entry.Source = EPCGExDataAssetEntrySource::Level;
			Entry.Level = WorldAsset;
			Entries.Add(Entry);
			continue;
		}

		// Try as UPCGDataAsset (DataAsset source)
		TSoftObjectPtr<UPCGDataAsset> Asset = TSoftObjectPtr<UPCGDataAsset>(SelectedAsset.ToSoftObjectPath());
		if (!Asset.LoadSynchronous()) { continue; }

		bool bAlreadyExists = false;
		for (const FPCGExPCGDataAssetCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.Source == EPCGExDataAssetEntrySource::DataAsset && ExistingEntry.DataAsset == Asset)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists) { continue; }

		FPCGExPCGDataAssetCollectionEntry Entry;
		Entry.Source = EPCGExDataAssetEntrySource::DataAsset;
		Entry.DataAsset = Asset;
		Entries.Add(Entry);
	}
}
#endif
