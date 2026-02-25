// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExActorCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "Engine/World.h"
#endif

#include "PCGComponent.h"
#include "PCGExLog.h"
#include "PCGExCollectionsSettingsCache.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Helpers/PCGExActorPropertyDelta.h"

// Static-init type registration: TypeId=Actor, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(Actor, UPCGExActorCollection, FPCGExActorCollectionEntry, "Actor Collection", Base)

UPCGExActorCollection::UPCGExActorCollection()
{
	const auto& Settings = PCGEX_COLLECTIONS_SETTINGS;

	UClass* EvalClass = Settings.DefaultBoundsEvaluatorClass
		? Settings.DefaultBoundsEvaluatorClass.Get()
		: UPCGExDefaultBoundsEvaluator::StaticClass();

	BoundsEvaluator = NewObject<UPCGExBoundsEvaluator>(this, EvalClass, TEXT("BoundsEvaluator"));
}

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

		// Compute bounds via evaluator or fallback
		const UPCGExActorCollection* ActorCollection = CastChecked<UPCGExActorCollection>(OwningCollection);
		if (ActorCollection->BoundsEvaluator)
		{
			const FBox WorldBounds = ActorCollection->BoundsEvaluator->EvaluateActorBounds(
				TempActor, const_cast<UPCGExActorCollection*>(ActorCollection), InInternalIndex);
			Staging.Bounds = WorldBounds.IsValid ? WorldBounds : FBox(ForceInit);
		}
		else
		{
			FVector Origin, Extents;
			TempActor->GetActorBounds(false, Origin, Extents);
			Staging.Bounds = FBox(Origin - Extents, Origin + Extents);
		}

		// Inspect for PCG components
		TInlineComponentArray<UPCGComponent*, 1> PCGComps;
		TempActor->GetComponents(PCGComps);
		bHasPCGComponent = !PCGComps.IsEmpty();
		CachedPCGGraph = (bHasPCGComponent && PCGComps[0]->GetGraph())
			? TSoftObjectPtr<UPCGGraphInterface>(FSoftObjectPath(PCGComps[0]->GetGraph()))
			: nullptr;

		// Hide the actor to ensure it doesn't affect gameplay or rendering
		TempActor->SetActorHiddenInGame(true);
		TempActor->SetActorEnableCollision(false);

		// Destroy the temporary actor
		TempActor->Destroy();

#else
		Staging.Bounds = FBox(ForceInit);
		bHasPCGComponent = false;
		CachedPCGGraph = nullptr;
		UE_LOG(LogPCGEx, Error, TEXT("UpdateStaging called in non-editor context. This is not supported for Actor Collections."));
#endif
	}

#if WITH_EDITOR
	// Delta capture from a placed actor in a level
	if (DeltaSourceLevel.ToSoftObjectPath().IsValid() && DeltaSourceActorName != NAME_None)
	{
		TSharedPtr<FStreamableHandle> LevelHandle = PCGExHelpers::LoadBlocking_AnyThread(DeltaSourceLevel.ToSoftObjectPath());

		if (const UWorld* World = DeltaSourceLevel.Get())
		{
			AActor* FoundActor = nullptr;
			if (World->PersistentLevel)
			{
				for (AActor* LevelActor : World->PersistentLevel->Actors)
				{
					if (LevelActor && LevelActor->GetFName() == DeltaSourceActorName)
					{
						FoundActor = LevelActor;
						break;
					}
				}
			}

			if (FoundActor)
			{
				if (Actor.Get() && FoundActor->IsA(Actor.Get()))
				{
					SerializedPropertyDelta = PCGExActorDelta::SerializeActorDelta(FoundActor);
				}
				else if (!Actor.ToSoftObjectPath().IsValid())
				{
					// Auto-populate Actor class from the found actor
					Actor = TSoftClassPtr<AActor>(FSoftClassPath(FoundActor->GetClass()));
					Staging.Path = Actor.ToSoftObjectPath();
					SerializedPropertyDelta = PCGExActorDelta::SerializeActorDelta(FoundActor);
				}
				else
				{
					UE_LOG(LogPCGEx, Warning, TEXT("Delta source actor class mismatch — expected '%s', found '%s'"),
						*Actor.ToSoftObjectPath().ToString(), *FSoftClassPath(FoundActor->GetClass()).ToString());
				}
			}
			else
			{
				UE_LOG(LogPCGEx, Warning, TEXT("Delta source actor '%s' not found in level '%s'"),
					*DeltaSourceActorName.ToString(), *DeltaSourceLevel.ToSoftObjectPath().ToString());
			}
		}

		PCGExHelpers::SafeReleaseHandle(LevelHandle);
	}
#endif

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

		if (!Actor.ToSoftObjectPath().IsValid())
		{
			bHasPCGComponent = false;
			CachedPCGGraph = nullptr;
		}

		// Clear stale delta if source level reference was removed
		if (!DeltaSourceLevel.ToSoftObjectPath().IsValid() || DeltaSourceActorName == NAME_None)
		{
			SerializedPropertyDelta.Empty();
		}
	}
	else
	{
		InternalSubCollection = SubCollection;
		bHasPCGComponent = false;
		CachedPCGGraph = nullptr;
		SerializedPropertyDelta.Empty();
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
