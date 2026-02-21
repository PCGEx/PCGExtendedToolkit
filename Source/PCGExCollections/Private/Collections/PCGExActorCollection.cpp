// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExActorCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "Engine/World.h"
#endif

#include "PCGExLog.h"
#include "Engine/Blueprint.h"

// Static-init type registration: TypeId=Actor, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(Actor, UPCGExActorCollection, FPCGExActorCollectionEntry, "Actor Collection", Base)

#pragma region FPCGExActorCollectionEntry

const UPCGExAssetCollection* FPCGExActorCollectionEntry::GetSubCollectionPtr() const
{
	return SubCollection;
}

void FPCGExActorCollectionEntry::ClearSubCollection()
{
	FPCGExAssetCollectionEntry::ClearSubCollection();
	SubCollection = nullptr;
}

bool FPCGExActorCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!Actor.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries) { return false; }
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

// Spawns a temporary actor in-editor to compute bounds via GetActorBounds(),
// then immediately destroys it. Only works in editor context (non-editor falls back to empty bounds).
void FPCGExActorCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Actor.ToSoftObjectPath();
	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(Actor.ToSoftObjectPath());

	if (UClass* ActorClass = Actor.Get())
	{
#if WITH_EDITOR
		UWorld* World = GWorld;
		if (!World)
		{
			UE_LOG(LogPCGEx, Error, TEXT("No world to compute actor bounds!"));
			return;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.bNoFail = true;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* TempActor = World->SpawnActor<AActor>(ActorClass, FTransform(), SpawnParams);
		if (!TempActor)
		{
			UE_LOG(LogPCGEx, Error, TEXT("Failed to create temp actor!"));
			return;
		}

		FVector Origin;
		FVector Extents;

		// Compute the bounds
		TempActor->GetActorBounds(bOnlyCollidingComponents, Origin, Extents, bIncludeFromChildActors);

		// Hide the actor to ensure it doesn't affect gameplay or rendering
		TempActor->SetActorHiddenInGame(true);
		TempActor->SetActorEnableCollision(false);

		// Destroy the temporary actor
		TempActor->Destroy();

		Staging.Bounds = FBox(Origin - Extents, Origin + Extents);

#else
		Staging.Bounds = FBox(ForceInit);
		UE_LOG(LogPCGEx, Error, TEXT("UpdateStaging called in non-editor context. This is not supported for Actor Collections."));
#endif
	}

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExActorCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	Actor = TSoftClassPtr<AActor>(InPath);
}

#if WITH_EDITOR
void FPCGExActorCollectionEntry::EDITOR_Sanitize()
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
}
#endif

#pragma endregion

#if WITH_EDITOR
void UPCGExActorCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		// Handle Blueprint assets
		if (SelectedAsset.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
		{
			const UBlueprint* Blueprint = Cast<UBlueprint>(SelectedAsset.GetAsset());
			if (!Blueprint || !Blueprint->GeneratedClass || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
			{
				continue;
			}

			TSoftClassPtr<AActor> ActorClass = TSoftClassPtr<AActor>(Blueprint->GeneratedClass.Get());

			bool bAlreadyExists = false;
			for (const FPCGExActorCollectionEntry& ExistingEntry : Entries)
			{
				if (ExistingEntry.Actor == ActorClass)
				{
					bAlreadyExists = true;
					break;
				}
			}

			if (bAlreadyExists) { continue; }

			FPCGExActorCollectionEntry Entry = FPCGExActorCollectionEntry();
			Entry.Actor = ActorClass;

			Entries.Add(Entry);
		}
	}
}
#endif
