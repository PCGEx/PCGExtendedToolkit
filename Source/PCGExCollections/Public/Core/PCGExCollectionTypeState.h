// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectSaveContext.h"

#include "PCGExCollectionTypeState.generated.h"

class UPCGExAssetCollection;

/**
 * Per-type collection machinery state/processor, hosted by heterogeneous collections (Omni).
 * The read-write sibling of the type-globals seam: where globals blocks are copy-out
 * SETTINGS, a type state owns the mutable, serialized, cross-entry STATE a collection type's
 * machinery operates on (e.g. the PCGDataAsset shared collections) AND doubles as the
 * processor -- the host dispatches its lifecycle into the hooks below and the state runs the
 * type's host-agnostic cores against itself.
 *
 * Registration: FTypeInfo::StateClass (via FTypeRegistry::AddPendingCustomization). A host
 * that can instantiate registered state classes answers SupportsTypeMachinery for that type;
 * Omni ensures one instance per present entry type at the start of every editor staging
 * rebuild session and serializes them (cooked too -- the state may carry runtime-consumed
 * subobjects).
 *
 * Hook contract mirrors the typed collections' own overrides -- see
 * UPCGExPCGDataAssetCollection for the reference semantics each hook ports:
 * - EDITOR_OnHostPostStagingRebuild: cross-entry post passes (fires after the host's native
 *   post-rebuild work, before staging pipelines' OnPostRebuild).
 * - OnHostPreSave: cook-time safety nets (called BEFORE Super::PreSave on the host).
 * - OnHostPostDuplicate: identity re-stamping after asset duplication.
 * - OnHostSerializeSave_Begin/End: paired around the host's Super::Serialize on save, for
 *   scrubbing instanced refs that live in the HOST's data (entry payload subobjects).
 *   State-OWNED instanced refs must be scrubbed in the state's own Serialize override
 *   instead -- the state is a separate package export and serializes outside that pair.
 * - AppendCookDependencyAssetPaths: extra cook references beyond the host's base walk.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class PCGEXCOLLECTIONS_API UPCGExCollectionTypeState : public UObject
{
	GENERATED_BODY()

public:
	virtual void OnHostPreSave(UPCGExAssetCollection* Host, FObjectPreSaveContext SaveContext)
	{
	}

	virtual void OnHostPostDuplicate(UPCGExAssetCollection* Host, bool bDuplicateForPIE)
	{
	}

	virtual void OnHostSerializeSave_Begin(UPCGExAssetCollection* Host)
	{
	}

	virtual void OnHostSerializeSave_End(UPCGExAssetCollection* Host)
	{
	}

#if WITH_EDITOR
	virtual void EDITOR_OnHostPostStagingRebuild(UPCGExAssetCollection* Host)
	{
	}

	virtual void AppendCookDependencyAssetPaths(const UPCGExAssetCollection* Host, TSet<FSoftObjectPath>& OutPaths) const
	{
	}

	/**
	 * Called once, right after a host creates this state (EnsureTypeSetup, or an explicit
	 * ensure during merge/convert). SeedSource is the collection whose entries prompted the
	 * creation when there is one (merge/conversion source; null for plain entry adds) --
	 * states may adopt its settings here. NEVER called on states that already exist: an
	 * existing state is the user's and always wins (same behavior-wins policy as
	 * globals-block merging).
	 */
	virtual void OnAddedToHost(UPCGExAssetCollection* Host, const UPCGExAssetCollection* SeedSource)
	{
	}

	/**
	 * Called instead of OnAddedToHost when an ensure-with-seed found this state already
	 * present (merge/conversion offered SeedSource, the existing state won). Implementations
	 * should surface meaningful conflicts -- e.g. warn when the ignored source carried
	 * different external-storage settings -- so the first-creator-wins policy is visible.
	 */
	virtual void OnSeedSourceIgnored(UPCGExAssetCollection* Host, const UPCGExAssetCollection* SeedSource)
	{
	}

	/**
	 * Called right before a host removes this state (EDITOR_CleanupUnusedTypeSetup) --
	 * last chance to release or surface owned resources (e.g. warn about externalized
	 * packages that will orphan). Removal proceeds regardless.
	 */
	virtual void OnRemovedFromHost(UPCGExAssetCollection* Host)
	{
	}
#endif
};
