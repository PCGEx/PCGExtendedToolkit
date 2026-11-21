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
	bDonorTaken = false;
}

void UPCGExManagedActors::DestroyDonors()
{
	TArray<TSoftObjectPtr<AActor>>& Actors = GetMutableGeneratedActors();
	if (Actors.Num() > NumSpawned)
	{
		PCGExHelpers::DestroyManagedActors(MakeArrayView(Actors).RightChop(NumSpawned));
		Actors.SetNum(NumSpawned);
	}

	NumDonors = NumSpawned;
}

void UPCGExManagedActors::PrepareSpawn(const UClass* InClass, FActorSpawnParameters& InOutSpawnParams)
{
	if (NumSpawned >= NumDonors)
	{
		return;
	}

	bDonorTaken = true;

	AActor* Donor = GetConstGeneratedActors()[NumSpawned].Get();
	if (!Donor)
	{
		return;
	}

	if (Donor->GetClass() == InClass && Donor->GetLevel() == InOutSpawnParams.OverrideLevel)
	{
		PCGExHelpers::InheritActorIdentity(Donor, InOutSpawnParams);
	}

	// Ahead of the spawn, one at a time: the successor's package derives from the name the donor still holds.
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

	// Not resolved once PrepareSpawn emptied it: by then the slot's path may name the successor that took over the name.
	if (!bDonorTaken)
	{
		PCGExHelpers::DestroyManagedActor(Actors[NumSpawned].Get());
	}
	bDonorTaken = false;

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
	DestroyDonors();
	ContentCrc = InContentCrc;
}
