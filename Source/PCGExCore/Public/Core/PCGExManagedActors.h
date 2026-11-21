// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "PCGCrc.h"
#include "PCGManagedResource.h"

#include "PCGExManagedActors.generated.h"

struct FActorSpawnParameters;

/**
 * Managed actors found again by slot (the base CRC, see FPCGExContext::ReclaimOrAddManagedActors), whether or not what they were spawned
 * from still holds. A run that respawns goes, per actor, [PrepareSpawn,] the spawn, CommitSpawn; then EndRespawn.
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

	/** Destroys in one go every actor the run has not replaced yet: for a run that hands no identity over. Game thread. */
	void DestroyDonors();

	/** Destroys the actor the next spawn replaces; an InClass actor of the level InOutSpawnParams spawns into hands its name and GUID over first. Game thread. */
	void PrepareSpawn(const UClass* InClass, FActorSpawnParameters& InOutSpawnParams);

	/** Tracks InActor in place of the actor it replaces, destroyed here if PrepareSpawn was skipped. Null when the spawn failed. Game thread. */
	void CommitSpawn(AActor* InActor);

	/** Destroys the actors the run did not replace and stamps InContentCrc. Game thread. */
	void EndRespawn(const FPCGCrc& InContentCrc);

private:
	/** Actors tracked when the run opened; those not replaced yet sit at [NumSpawned, NumDonors) */
	int32 NumDonors = 0;

	/** Actors committed so far, which is also the slot the next one goes to */
	int32 NumSpawned = 0;

	/** PrepareSpawn already emptied the slot the next CommitSpawn fills */
	bool bDonorTaken = false;
};
