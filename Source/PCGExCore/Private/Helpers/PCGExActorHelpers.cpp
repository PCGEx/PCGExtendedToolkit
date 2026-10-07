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

		for (int i = 0; i < ActorReferences->Values.Num(); i++)
		{
			const FSoftObjectPath& Path = ActorReferences->Values[i];
			if (!Path.IsValid())
			{
				continue;
			}
			if (AActor* TargetActor = Cast<AActor>(Path.ResolveObject()))
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
