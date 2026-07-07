// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "PCGExSchemaMerging.h"
#include "PCGGraph.h"
#include "Core/PCGExAssetCollection.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExBoundsEvaluator.h"

#include "PCGExActorCollection.generated.h"

class UPCGExActorCollection;

/**
 * Actor collection entry. References an actor class (TSoftClassPtr<AActor>) or, via the
 * base SubCollection property, any collection type as a subcollection. Simpler than
 * FPCGExMeshCollectionEntry -- no MicroCache, no descriptors. UpdateStaging() spawns a
 * temporary actor in-editor to compute bounds (with configurable collision/child-actor inclusion).
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Actor Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExActorCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExActorCollectionEntry() = default;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Actor;
	}

	// Actor-Specific Properties

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftClassPtr<AActor> Actor = nullptr;

	/** Cached: whether the actor CDO has any UPCGComponent. */
	UPROPERTY()
	bool bHasPCGComponent = false;

	/** Cached: graph set on the first found PCG component, if any. */
	UPROPERTY()
	TSoftObjectPtr<UPCGGraphInterface> CachedPCGGraph;

	/** Serialized property delta from CDO (UE tagged property format).
	 *  Empty = CDO-identical. Populated by level data exporter or delta source authoring. */
	UPROPERTY()
	TArray<uint8> SerializedPropertyDelta;

	/** Soft paths to assets / external objects referenced by SerializedPropertyDelta.
	 *  Captured at delta-build time alongside the bytes. The spawn element appends these
	 *  to its async load batch so FSoftObjectPath::ResolveObject inside the reader resolves
	 *  to live objects without sync-loading on the worker thread. Also doubles as a cook
	 *  reference declaration: the UPROPERTY array makes referenced assets discoverable to
	 *  the cooker, which would otherwise miss soft paths buried inside opaque byte data. */
	UPROPERTY()
	TArray<FSoftObjectPath> DeltaCollateralPaths;

	/** Optional: reference a specific level to capture property deltas from a placed actor.
	 *  The actor's class must match the Actor class ref. During UpdateStaging,
	 *  the property delta is computed from that instance vs its CDO. */
	UPROPERTY(EditAnywhere, Category = "Settings|Delta", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UWorld> DeltaSourceLevel;

	/** Name of the actor within the level to capture delta from. */
	UPROPERTY(EditAnywhere, Category = "Settings|Delta", meta=(EditCondition="!bIsSubCollection ", EditConditionHides))
	FName DeltaSourceActorName;

	// Lifecycle
	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;

#if WITH_EDITOR
	virtual void EDITOR_Sanitize() override;
#endif
};

/** Concrete collection for actor classes. Minimal extension of the base -- no extra
 *  global settings beyond what UPCGExAssetCollection provides. */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Actor", meta=(ToolTip = "A weighted collection of actor classes for spawning."))
class PCGEXCOLLECTIONS_API UPCGExActorCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExActorCollectionEntry)

public:
	UPCGExActorCollection(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	friend struct FPCGExActorCollectionEntry;

	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Actor;
	}

	/** Bounds evaluator for bounds computation. If null, basic GetActorBounds fallback is used. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Settings|Import")
	TObjectPtr<UPCGExBoundsEvaluator> BoundsEvaluator;

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExActorCollectionEntry> Entries;

	/**
	 * Policy used when merging actor-component schemas into CollectionProperties on staging
	 * rebuild. Applies to the editor auto-rebuild path (EDITOR_OnPostStagingRebuild).
	 *
	 * For embedded collections built by UPCGExDefaultLevelDataExporter, this field is
	 * overwritten on each export to mirror the exporter's SchemaMergePolicy so manual
	 * rebuilds of the embedded asset stay consistent with how it was generated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Import")
	EPCGExSchemaMergePolicy SchemaMergePolicy = EPCGExSchemaMergePolicy::StrictTypeMatch;

	/**
	 * Scan each entry's source actor for a UPCGExPropertyCollectionComponent, merge its
	 * schemas into CollectionProperties, and populate per-entry PropertyOverrides from the
	 * component's authored values.
	 *
	 * Source resolution per entry:
	 *   1. If RepresentativeInstances is non-empty and its slot at the entry's index is
	 *      non-null, that live actor is the donor. (Level-exporter path: instance in-hand.)
	 *   2. Else, if entry.DeltaSourceLevel + DeltaSourceActorName resolves to a placed actor
	 *      whose class matches entry.Actor, that actor is the donor. (Standalone authored
	 *      path: user pointed the entry at a level-placed actor.)
	 *   3. Else, the entry contributes no schema and gets no overrides (its row in the merged
	 *      override array is left disabled).
	 *
	 * Existing manually-authored CollectionProperties participates in the merge as source #0,
	 * so under FirstWins / StrictTypeMatch (default) manual entries are always preserved.
	 *
	 * Auto-invoked at the tail of editor-driven staging rebuilds via EDITOR_OnPostStagingRebuild.
	 * The level exporter calls it explicitly with a representative-instance map before invoking
	 * RebuildStagingData on the embedded collection.
	 *
	 * @param Policy            Conflict resolution policy. Defaults to StrictTypeMatch.
	 * @param RepresentativeInstances Parallel to Entries; entries with null/missing slots fall
	 *                          through to DeltaSourceLevel resolution. May be empty.
	 */
	void RebuildPropertiesFromActorComponents(
		EPCGExSchemaMergePolicy Policy = EPCGExSchemaMergePolicy::StrictTypeMatch,
		TArrayView<AActor*> RepresentativeInstances = {});

#if WITH_EDITOR
	// Editor Functions
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
	virtual void EDITOR_OnPostStagingRebuild() override;
#endif
};
