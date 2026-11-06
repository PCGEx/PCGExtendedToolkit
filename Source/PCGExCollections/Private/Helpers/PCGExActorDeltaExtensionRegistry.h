// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "Helpers/PCGExActorPropertyDelta.h"

/** The delta extension registrations, read by capture and apply. Game thread only, unsynchronized. */
namespace PCGExActorDelta::ExtensionRegistry
{
	/** Resolves the pending soft registrations, and sends back to pending one whose class was collected (a Blueprint
	 *  reload) so it resolves again from its path. A pass over the registrations, cheap enough to run on every capture
	 *  and apply instead of listening for module loads. */
	void TryResolvePending();

	/** Live registrations matching Class or one of its supers, most-derived first. */
	void GatherMatching(UClass* Class, TArray<uint64, TInlineAllocator<8>>& OutIds);

	/** Id's registration, or null once it is unregistered. */
	const FDeltaExtension* Find(uint64 Id);

	/** The live, resolved registration of a payload Id, and its class. */
	const FDeltaExtension* FindById(FName PayloadId, UClass*& OutClass);

	/** Properties the extensions matching Class keep out of the generic delta. */
	void GatherClaims(UClass* Class, TArray<FName, TInlineAllocator<4>>& OutClaims);

	/** Runs the PostApply of the extensions matching each of Actor's components. */
	void RunPostApply(AActor* Actor);

	/** Brackets a call into an extension: entries stay put while it runs, removals land when the outermost one returns. */
	struct FCallbackScope
	{
		FCallbackScope();
		~FCallbackScope();

		FCallbackScope(const FCallbackScope&) = delete;
		FCallbackScope& operator=(const FCallbackScope&) = delete;
	};
}
