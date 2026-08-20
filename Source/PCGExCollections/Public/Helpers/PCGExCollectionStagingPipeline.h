// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExCollectionStagingPipeline.generated.h"

class UPCGExAssetCollection;

/**
 * Editor-only post-process pipeline for asset-collection staging rebuilds.
 *
 * Subclass in C++ or Blueprint and add to UPCGExAssetCollection::StagingPipelines to
 * post-process entry data (property overrides, tags, weights, ...) whenever the collection's
 * staging data is rebuilt in the editor. Instanced via Instanced/EditInlineNew so derived
 * classes can expose their own UPROPERTYs directly in the collection's details panel.
 * Pipelines compose: every assigned pipeline runs in array order at each hook point (null
 * slots are skipped), so later pipelines observe earlier pipelines' mutations.
 *
 * Hook contract:
 *  - Fires for every editor rebuild session: toolbar Rebuild / Rebuild Recursive / Rebuild
 *    Project, single-entry restages (grid edits), stale-entry batches and property-edit
 *    triggered rebuilds. Pre/post fire once per session; OnProcessEntry fires once per
 *    restaged entry (Sanitize -> UpdateStaging -> PostUpdateStaging -> OnProcessEntry).
 *  - OnPostRebuild runs AFTER the native EDITOR_OnPostStagingRebuild extension point (actor
 *    component schema merges, shared-collection compaction), so the pipeline gets final say.
 *  - Subcollections recursed into via the runtime RebuildStagingData do NOT run their own
 *    pipelines; only the collection whose editor rebuild was triggered dispatches hooks.
 *  - Rebuilds triggered from inside a hook run WITHOUT re-firing hooks (no recursion).
 *  - Hooks never fire while cooking.
 *
 * Mutate entry VALUES only from OnProcessEntry -- never add or remove entries there, the
 * entry array is being iterated live. Structural changes belong in OnPreRebuild/OnPostRebuild.
 *
 * Events are declared unconditionally so Blueprint subclasses stay cook-safe, but they are
 * only ever invoked from WITH_EDITOR rebuild paths (the collection-side StagingPipelines
 * list is editor-only data and never exists in cooked targets).
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, meta=(DisplayName="Collection Staging Pipeline", PCGExNodeLibraryDoc="staging/collections/helpers/collection-staging-pipeline"))
class PCGEXCOLLECTIONS_API UPCGExCollectionStagingPipeline : public UObject
{
	GENERATED_BODY()

	// The dispatcher stamps the hook-scoped target context (collection + entry index) around
	// each event invocation.
	friend class UPCGExAssetCollection;

public:
	/**
	 * The collection currently dispatching hooks into this pipeline. Self-context: the self
	 * pin is hidden and non-connectable (HideSelfPin), so the node renders as a tiny output
	 * lozenge that can be dropped next to any library node instead of dragging the event's
	 * Collection pin across the canvas. Only meaningful on the executing pipeline; null
	 * outside hook execution (e.g. when called from an editor utility widget).
	 */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection|Staging", meta = (CompactNodeTitle = "Collection", HideSelfPin = "true"))
	UPCGExAssetCollection* GetTargetCollection() const
	{
		return TargetCollection;
	}

	/** The entry index currently being processed. Only valid inside OnProcessEntry; -1 in
	 *  OnPreRebuild / OnPostRebuild and outside hook execution. */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection|Staging", meta = (CompactNodeTitle = "Entry", HideSelfPin = "true"))
	int32 GetTargetEntryIndex() const
	{
		return TargetEntryIndex;
	}

	/** True while executing OnProcessEntry (i.e. GetTargetEntryIndex is a valid index). */
	UFUNCTION(BlueprintPure, Category = "PCGEx|Collection|Staging", meta = (HideSelfPin = "true"))
	bool IsProcessingEntry() const
	{
		return TargetEntryIndex >= 0;
	}

	/** Fired once per rebuild session, before any entry is re-staged. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Collection|Staging")
	void OnPreRebuild(UPCGExAssetCollection* Collection);

	virtual void OnPreRebuild_Implementation(UPCGExAssetCollection* Collection)
	{
	}

	/** Fired for each re-staged entry, after its UpdateStaging + PostUpdateStaging.
	 *  bIsSubCollection is true when the entry references a nested collection rather than an asset. */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Collection|Staging")
	void OnProcessEntry(UPCGExAssetCollection* Collection, int32 EntryIndex, bool bIsSubCollection);

	virtual void OnProcessEntry_Implementation(UPCGExAssetCollection* Collection, int32 EntryIndex, bool bIsSubCollection)
	{
	}

	/** Fired once per rebuild session, after every entry has been re-staged AND the native
	 *  EDITOR_OnPostStagingRebuild extension point has run (schema merges, compaction). */
	UFUNCTION(BlueprintNativeEvent, Category = "PCGEx|Collection|Staging")
	void OnPostRebuild(UPCGExAssetCollection* Collection);

	virtual void OnPostRebuild_Implementation(UPCGExAssetCollection* Collection)
	{
	}

protected:
	/** Hook-scoped dispatch context. Stamped by the owning collection's dispatcher around each
	 *  event (single-threaded, re-entrancy guarded), cleared on scope exit. Transient on
	 *  purpose: never serialized, only meaningful mid-dispatch. */
	UPROPERTY(Transient)
	TObjectPtr<UPCGExAssetCollection> TargetCollection;

	int32 TargetEntryIndex = -1;
};
