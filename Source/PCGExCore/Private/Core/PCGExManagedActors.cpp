// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Core/PCGExManagedActors.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExActorHelpers.h"

bool UPCGExManagedActors::IsCurrent(const FPCGCrc& InContentCrc, const int32 InNumActors) const
{
	return ContentCrc == InContentCrc && (InNumActors == INDEX_NONE || GetConstGeneratedActors().Num() == InNumActors);
}

void UPCGExManagedActors::BeginRespawn()
{
	ContentCrc = FPCGCrc();
	NumDonors = GetConstGeneratedActors().Num();
	NumSpawned = 0;
}

void UPCGExManagedActors::PrepareSpawn(const UClass* InClass, const ULevel* InLevel, FActorSpawnParameters& InOutSpawnParams)
{
	AActor* Donor = NumSpawned < NumDonors ? GetConstGeneratedActors()[NumSpawned].Get() : nullptr;
	if (!Donor)
	{
		return;
	}

	if (Donor->GetClass() == InClass && Donor->GetLevel() == InLevel)
	{
		PCGExHelpers::InheritActorIdentity(Donor, InOutSpawnParams);
	}

	PCGExHelpers::DestroyManagedActor(Donor);
}

void UPCGExManagedActors::CommitSpawn(AActor* InActor)
{
	// Slot by slot rather than into a fresh list: an interrupted run leaves every actor, old or new, tracked.
	TArray<TSoftObjectPtr<AActor>>& Actors = GetMutableGeneratedActors();

	if (NumSpawned >= NumDonors)
	{
		if (InActor)
		{
			Actors.Add(InActor);
			NumSpawned++;
		}
		return;
	}

	// A soft pointer re-resolves by path: once the replaced actor is gone, its slot can resolve to a successor of the same name.
	if (AActor* Replaced = Actors[NumSpawned].Get(); Replaced != InActor)
	{
		PCGExHelpers::DestroyManagedActor(Replaced);
	}

	if (!InActor)
	{
		Actors.RemoveAt(NumSpawned);
		NumDonors--;
		return;
	}

	Actors[NumSpawned++] = InActor;
}

void UPCGExManagedActors::EndRespawn(const FPCGCrc& InContentCrc)
{
	TArray<TSoftObjectPtr<AActor>>& Actors = GetMutableGeneratedActors();
	for (int32 i = NumSpawned; i < Actors.Num(); i++)
	{
		PCGExHelpers::DestroyManagedActor(Actors[i].Get());
	}
	Actors.SetNum(NumSpawned);

	ContentCrc = InContentCrc;
}
