// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCrc.h"
#include "PCGManagedResource.h"

#include "PCGExManagedActors.generated.h"

class ULevel;

struct FActorSpawnParameters;

/**
 * Managed actors found again by slot (the base CRC, see FPCGExContext::ReclaimManagedActors), whether or not what they were
 * spawned from still holds. A run that spawns goes BeginRespawn, then per actor [PrepareSpawn,] the spawn, CommitSpawn, then EndRespawn.
 */
UCLASS()
class PCGEXCORE_API UPCGExManagedActors : public UPCGManagedActors
{
	GENERATED_BODY()

public:
	/** Hash of what the actors were spawned from. Invalid while they are being spawned, so an interrupted run never passes for a complete one. */
	UPROPERTY()
	FPCGCrc ContentCrc;

	/** Whether the actors are still what InContentCrc spawns. InNumActors is what a run without a failed spawn leaves, when the caller can tell. */
	bool IsCurrent(const FPCGCrc& InContentCrc, int32 InNumActors = INDEX_NONE) const;

	/** Opens a run that replaces the tracked actors one by one, in order. */
	void BeginRespawn();

	/** Destroys the actor the next spawn replaces; an InClass actor of InLevel hands its name and GUID over to InOutSpawnParams first. Game thread. */
	void PrepareSpawn(const UClass* InClass, const ULevel* InLevel, FActorSpawnParameters& InOutSpawnParams);

	/** Tracks InActor in place of the actor it replaces, destroyed here if PrepareSpawn was skipped. Null when the spawn failed. Game thread. */
	void CommitSpawn(AActor* InActor);

	/** Destroys the actors the run did not replace and stamps InContentCrc. Game thread. */
	void EndRespawn(const FPCGCrc& InContentCrc);

private:
	/** Actors tracked when the run opened; those not replaced yet sit at [NumSpawned, NumDonors) */
	int32 NumDonors = 0;

	/** Actors committed so far, which is also the slot the next one goes to */
	int32 NumSpawned = 0;
};
