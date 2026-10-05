// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#endif

namespace PCGExEditor
{
#if WITH_EDITOR
	/**
	 * Fire the standard "object changed externally" notification pair: BroadcastOnObjectModified
	 * (UI/details refresh) + OnObjectPropertyChanged with an empty event (PCG asset trackers,
	 * other systems that listen to property edits but won't see a property-specific event).
	 *
	 * Use this when mutating a UObject outside a PostEditChangeProperty path -- e.g. a
	 * programmatic rebuild -- to keep downstream consumers in sync the same way they would
	 * be after an edit through the property editor.
	 *
	 * Game thread only: PCG's tracking manager refreshes tracked components synchronously from the
	 * broadcast (IPCGBaseSubsystem::CancelGeneration asserts it). An off-thread call is dropped.
	 */
	FORCEINLINE void NotifyObjectChanged(UObject* Object)
	{
		if (!Object)
		{
			return;
		}
		if (!ensureMsgf(IsInGameThread(), TEXT("NotifyObjectChanged('%s') called off the game thread; notification dropped."), *Object->GetName()))
		{
			return;
		}
		FCoreUObjectDelegates::BroadcastOnObjectModified(Object);
		FPropertyChangedEvent EmptyEvent(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Object, EmptyEvent);
	}
#endif
}
