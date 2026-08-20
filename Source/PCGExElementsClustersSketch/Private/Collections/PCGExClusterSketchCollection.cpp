// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExClusterSketchCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#endif

#include "Helpers/PCGExStreamingHelpers.h"

// Registered from StartupModule, not a static initializer -- see the header for why.
void PCGExSketch::RegisterCollectionType()
{
	using namespace PCGExAssetCollection;

	FTypeInfo Info;
	Info.Id = PCGExSketch::CollectionTypeId;
	Info.CollectionClass = UPCGExClusterSketchCollection::StaticClass();
	Info.EntryStruct = FPCGExClusterSketchCollectionEntry::StaticStruct();
	Info.DisplayName = NSLOCTEXT("PCGEx", "ClusterSketchCollection", "Cluster Sketch Collection");
	Info.ParentType = TypeIds::Base;
	FTypeRegistry::Get().Register(Info);

#if WITH_EDITOR
	// Omni drag-drop ingestion. Must follow Register: Customize mutates an existing entry.
	FTypeRegistry::Get().Customize(
		PCGExSketch::CollectionTypeId,
		[](FTypeInfo& Info)
		{
			Info.SourceDetectPriority = 15;
			Info.DetectSourceAsset = [](const FAssetData& Asset) { return Asset.IsInstanceOf<UPCGExClusterSketch>(); };
		});
#endif
}

#pragma region FPCGExClusterSketchCollectionEntry

bool FPCGExClusterSketchCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!Sketch.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
		{
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

void FPCGExClusterSketchCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	// Authored staging carries authoritative bounds from an external system; refresh identity only.
	if (Staging.bAuthored && !bIsSubCollection)
	{
		Staging.Path = Sketch.ToSoftObjectPath();
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Sketch.ToSoftObjectPath();

	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThreadTpl(Sketch);

	if (const UPCGExClusterSketch* S = Sketch.Get())
	{
		// GetBounds resolves lattice-bound vertices through the snap provider's basis, and the provider
		// may need its own soft refs resolved first (see UPCGExClusterSnapProvider::CollectAssetDependencies);
		// without them BuildBasis fails and bounds silently fall back to the authored transforms.
		TArray<FSoftObjectPath> NestedPaths;
		S->CollectAssetDependencies(NestedPaths);

		TSharedPtr<FStreamableHandle> NestedHandle;
		if (!NestedPaths.IsEmpty())
		{
			const TSharedPtr<TSet<FSoftObjectPath>> UniqueNested = MakeShared<TSet<FSoftObjectPath>>(NestedPaths);
			NestedHandle = PCGExHelpers::LoadBlocking_AnyThread(UniqueNested);
		}

		Staging.Bounds = S->GetBounds();
		PCGExHelpers::SafeReleaseHandle(NestedHandle);
	}
	else
	{
		Staging.Bounds = FBox(ForceInit);
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExClusterSketchCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	Sketch = TSoftObjectPtr<UPCGExClusterSketch>(InPath);
}

#pragma endregion

#pragma region UPCGExClusterSketchCollection

#if WITH_EDITOR
void UPCGExClusterSketchCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		TSoftObjectPtr<UPCGExClusterSketch> Candidate = TSoftObjectPtr<UPCGExClusterSketch>(SelectedAsset.ToSoftObjectPath());
		if (!Candidate.LoadSynchronous())
		{
			continue;
		}

		bool bAlreadyExists = false;
		for (const FPCGExClusterSketchCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.Sketch == Candidate)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}

		FPCGExClusterSketchCollectionEntry Entry = FPCGExClusterSketchCollectionEntry();
		Entry.Sketch = Candidate;

		Entries.Add(Entry);
	}
}
#endif

#pragma endregion
