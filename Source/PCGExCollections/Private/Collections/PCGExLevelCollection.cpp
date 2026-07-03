// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExLevelCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#endif

#include "PCGExCollectionsSettingsCache.h"
#include "PCGExLog.h"
#include "PCGExSocketProvider.h"
#include "Helpers/PCGExBoundsEvaluator.h"

// Static-init type registration: TypeId=Level, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(Level, UPCGExLevelCollection, FPCGExLevelCollectionEntry, "Level Collection", Base)

UPCGExLevelCollection::UPCGExLevelCollection(const FObjectInitializer& ObjectInitializer)
{
	const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;

	UClass* FilterClass = Settings.DefaultContentFilterClass
		? Settings.DefaultContentFilterClass.Get()
		: UPCGExDefaultActorContentFilter::StaticClass();

	UClass* EvalClass = Settings.DefaultBoundsEvaluatorClass
		? Settings.DefaultBoundsEvaluatorClass.Get()
		: UPCGExDefaultBoundsEvaluator::StaticClass();

	ContentFilter = Cast<UPCGExActorContentFilter>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("ContentFilter"),
		                                         UPCGExActorContentFilter::StaticClass(), FilterClass, false, false));

	BoundsEvaluator = Cast<UPCGExBoundsEvaluator>(
		ObjectInitializer.CreateDefaultSubobject(this, TEXT("BoundsEvaluator"),
		                                         UPCGExBoundsEvaluator::StaticClass(), EvalClass, false, false));
}

#pragma region FPCGExLevelCollectionEntry

bool FPCGExLevelCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!Level.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries)
		{
			return false;
		}
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

void FPCGExLevelCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Level.ToSoftObjectPath();
	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(Level.ToSoftObjectPath());

#if WITH_EDITOR
	if (const UWorld* World = Level.Get())
	{
		const UPCGExLevelCollection* LevelCollection = CastChecked<UPCGExLevelCollection>(OwningCollection);

		FBox CombinedBounds(ForceInit);

		if (World->PersistentLevel)
		{
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				if (!Actor)
				{
					continue;
				}

				// Extract sockets from socket actors before content filter.
				// StaticPassesFilter will then reject actors with ShouldStripFromExport=true,
				// keeping them out of bounds computation automatically.
				if (IPCGExSocketProvider* Provider = Cast<IPCGExSocketProvider>(Actor))
				{
					FPCGExSocket& NewSocket = Staging.Sockets.Emplace_GetRef(
						Provider->GetSocketName_Implementation(),
						Provider->GetSocketTransform_Implementation(),
						Provider->GetSocketTag_Implementation());
					NewSocket.bManaged = true;
				}

				if (!UPCGExActorContentFilter::StaticPassesFilter(
					LevelCollection->ContentFilter, Actor,
					const_cast<UPCGExLevelCollection*>(LevelCollection), InInternalIndex))
				{
					continue;
				}

				if (LevelCollection->BoundsEvaluator)
				{
					CombinedBounds += LevelCollection->BoundsEvaluator->EvaluateActorBounds(
						Actor, const_cast<UPCGExLevelCollection*>(LevelCollection), InInternalIndex);
				}
			}
		}

		Staging.Bounds = CombinedBounds.IsValid ? CombinedBounds : FBox(ForceInit);
	}
	else
	{
		Staging.Bounds = FBox(ForceInit);
	}
#else
	Staging.Bounds = FBox(ForceInit);
	UE_LOG(LogPCGEx, Error, TEXT("UpdateStaging called in non-editor context. This is not supported for Level Collections."));
#endif

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExLevelCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	Level = TSoftObjectPtr<UWorld>(InPath);
}

#if WITH_EDITOR
void FPCGExLevelCollectionEntry::EDITOR_GetSourceAssetPaths(TSet<FSoftObjectPath>& OutPaths) const
{
	if (bIsSubCollection)
	{
		return;
	}

	// Advertise Level independent of Staging.Path so rebuild fires even on fresh entries
	// that haven't been staged yet.
	const FSoftObjectPath LevelPath = Level.ToSoftObjectPath();
	if (LevelPath.IsValid())
	{
		OutPaths.Emplace(LevelPath);
	}
}
#endif

#pragma endregion

#if WITH_EDITOR
void UPCGExLevelCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		// Accept UWorld assets (.umap files)
		if (SelectedAsset.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
		{
			continue;
		}

		TSoftObjectPtr<UWorld> LevelPtr(SelectedAsset.GetSoftObjectPath());

		// Dedup check
		bool bAlreadyExists = false;
		for (const FPCGExLevelCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.Level == LevelPtr)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists)
		{
			continue;
		}

		FPCGExLevelCollectionEntry Entry = FPCGExLevelCollectionEntry();
		Entry.Level = LevelPtr;

		Entries.Add(Entry);
	}
}
#endif
