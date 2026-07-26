// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorHelpers.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectThreadContext.h"

namespace PCGExHelpers
{
	bool IsSpawnSafe(const UWorld* InWorld)
	{
		// Spawning runs construction scripts, and ProcessEvent hard-asserts while the loader is
		// routing PostLoad -- no world is spawn-safe during that window regardless of its own state.
		if (FUObjectThreadContext::Get().IsRoutingPostLoad)
		{
			return false;
		}

		return InWorld
			&& !InWorld->bIsTearingDown
			&& InWorld->PersistentLevel
			&& InWorld->WorldType != EWorldType::Inactive
			&& InWorld->WorldType != EWorldType::None;
	}

	bool WithTempActor(UWorld* InWorld, UClass* InClass, TFunctionRef<void(AActor*)> Body)
	{
		if (!InClass || !IsSpawnSafe(InWorld))
		{
			return false;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.bNoFail = true;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* TempActor = InWorld->SpawnActor<AActor>(InClass, FTransform(), SpawnParams);
		if (!TempActor)
		{
			return false;
		}

		Body(TempActor);

		// Hide before destroying so the actor can't affect rendering or gameplay in the frame it
		// lives for.
		TempActor->SetActorHiddenInGame(true);
		TempActor->SetActorEnableCollision(false);
		TempActor->Destroy();

		return true;
	}
}
