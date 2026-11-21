// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Helpers/PCGExActorHelpers.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGExCoreMacros.h"
#include "PCGExVersion.h"
#include "PCGGraphExecutionStateInterface.h"
#include "PCGModule.h"
#include "Components/SceneComponent.h"
#include "Data/PCGExAttributeBroadcaster.h"
#include "Data/PCGExData.h"
#include "Data/PCGExPointIO.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGActorHelpers.h"
#include "Helpers/PCGExStreamingHelpers.h"
#include "UObject/UObjectThreadContext.h"

namespace PCGExHelpers
{
	bool GetIncludedActors(const FPCGContext* InContext, const TSharedRef<PCGExData::FFacade>& InFacade, const FName ActorReferenceName, TMap<AActor*, int32>& OutActorSet)
	{
		FPCGAttributePropertyInputSelector Selector = FPCGAttributePropertyInputSelector();
		Selector.SetAttributeName(ActorReferenceName);

		const TUniquePtr<PCGExData::TAttributeBroadcaster<FSoftObjectPath>> ActorReferences = MakeUnique<PCGExData::TAttributeBroadcaster<FSoftObjectPath>>();
		if (!ActorReferences->Prepare(Selector, InFacade->Source))
		{
			PCGE_LOG_C(Error, GraphAndLog, InContext, FTEXT("Actor reference attribute does not exist."));
			return false;
		}

		ActorReferences->Grab();

		// One GC-guarded batch (callers boot on workers); rows repeat actors, each unique path resolves once.
		TArray<UObject*> Resolved;
		ResolveObjects_AnyThread(ActorReferences->Values, Resolved);

		for (int i = 0; i < Resolved.Num(); i++)
		{
			if (AActor* TargetActor = Cast<AActor>(Resolved[i]))
			{
				OutActorSet.FindOrAdd(TargetActor, i);
			}
		}

		return true;
	}

	AActor* GetSourceActor(const IPCGGraphExecutionSource* InSource)
	{
		if (!InSource) { return nullptr; }
#if PCGEX_ENGINE_VERSION >= 508
		return InSource->GetExecutionState().GetTypedTarget<AActor>();
#else
		const UPCGComponent* Component = Cast<UPCGComponent>(InSource);
		return Component ? Component->GetOwner() : nullptr;
#endif
	}

	bool IsSourceInPreviewMode(const IPCGGraphExecutionSource* InSource)
	{
		if (!InSource) { return false; }
#if PCGEX_ENGINE_VERSION >= 508
		return InSource->GetExecutionState().IsInPreviewMode();
#else
		const UPCGComponent* Component = Cast<UPCGComponent>(InSource);
		return Component && Component->IsInPreviewMode();
#endif
	}

	bool IsSourceInPreviewMode(const FPCGContext* InContext)
	{
		return InContext && IsSourceInPreviewMode(InContext->ExecutionSource.Get());
	}

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

	void InheritActorIdentity(const AActor* InDonor, FActorSpawnParameters& InOutSpawnParams)
	{
#if WITH_EDITOR
		if (!InDonor || InDonor->HasAnyFlags(RF_Transient) || EnumHasAnyFlags(InOutSpawnParams.ObjectFlags, RF_Transient))
		{
			return;
		}

		InOutSpawnParams.Name = InDonor->GetFName();
		// Not the default Required_Fatal: when the name cannot be used the engine falls back to a new one.
		InOutSpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		// The package is derived from the level and the name; the GUID keeps the actor descriptor the same one.
		InOutSpawnParams.OverrideActorGuid = InDonor->GetActorGuid();
#endif
	}

	void DestroyManagedActors(const TConstArrayView<TSoftObjectPtr<AActor>> InActors)
	{
		UWorld* World = nullptr;
		TInlineComponentArray<UPCGComponent*, 1> Components;

		for (const TSoftObjectPtr<AActor>& ActorPtr : InActors)
		{
			AActor* Actor = ActorPtr.Get();
			if (!Actor)
			{
				continue;
			}

			if (!World)
			{
				World = Actor->GetWorld();
			}

			Components.Reset();
			Actor->GetComponents(Components);
			for (UPCGComponent* Component : Components)
			{
				Component->CleanupLocalImmediate(/*bRemoveComponents=*/true);
			}
		}

		if (World)
		{
			UPCGActorHelpers::DeleteActors(World, TArray<TSoftObjectPtr<AActor>>(InActors));
		}
	}

	void DestroyManagedActor(AActor* InActor)
	{
		if (!InActor)
		{
			return;
		}

		const TSoftObjectPtr<AActor> ActorPtr(InActor);
		DestroyManagedActors(MakeArrayView(&ActorPtr, 1));

#if WITH_EDITOR
		// A destroyed actor holds its name until collected. UObject::Rename, not AActor's: the outer stays the level.
		InActor->UObject::Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
#endif
	}

	void EnsureWorldTransformsCurrent(UWorld* InWorld)
	{
		if (!InWorld || !InWorld->PersistentLevel)
		{
			return;
		}

		TInlineComponentArray<USceneComponent*> SceneComponents;
		for (AActor* Actor : InWorld->PersistentLevel->Actors)
		{
			if (!Actor)
			{
				continue;
			}

			SceneComponents.Reset();
			Actor->GetComponents<USceneComponent>(SceneComponents);
			for (USceneComponent* Component : SceneComponents)
			{
				// Per-component (not per-root): AttachChildren is rebuilt at registration, so on an
				// unregistered world a parent's update does not propagate down. Parent-first ordering
				// is handled inside the engine call regardless of iteration order.
				Component->ConditionalUpdateComponentToWorld();
			}
		}
	}
}
