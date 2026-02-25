// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExBoundsEvaluator.h"
#include "PCGGraph.h"

#include "PCGExActorCollection.generated.h"

class UPCGExActorCollection;

/**
 * Actor collection entry. References an actor class (TSoftClassPtr<AActor>) or
 * a UPCGExActorCollection subcollection. Simpler than FPCGExMeshCollectionEntry --
 * no MicroCache, no descriptors. UpdateStaging() spawns a temporary actor in-editor
 * to compute bounds (with configurable collision/child-actor inclusion).
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Actor Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExActorCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExActorCollectionEntry() = default;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override { return PCGExAssetCollection::TypeIds::Actor; }

	// Actor-Specific Properties
	
	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftClassPtr<AActor> Actor = nullptr;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bIsSubCollection", EditConditionHides, DisplayAfter="bIsSubCollection"))
	TObjectPtr<UPCGExActorCollection> SubCollection;

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

	/** Optional: reference a specific level to capture property deltas from a placed actor.
	 *  The actor's class must match the Actor class ref. During UpdateStaging,
	 *  the property delta is computed from that instance vs its CDO. */
	UPROPERTY(EditAnywhere, Category = "Settings|Delta", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UWorld> DeltaSourceLevel;

	/** Name of the actor within the level to capture delta from. */
	UPROPERTY(EditAnywhere, Category = "Settings|Delta", meta=(EditCondition="!bIsSubCollection ", EditConditionHides))
	FName DeltaSourceActorName;

	virtual const UPCGExAssetCollection* GetSubCollectionPtr() const override;

	virtual void ClearSubCollection() override;

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
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Actor")
class PCGEXCOLLECTIONS_API UPCGExActorCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExActorCollectionEntry)

public:
	UPCGExActorCollection();

	friend struct FPCGExActorCollectionEntry;

	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Actor;
	}

	/** Bounds evaluator for bounds computation. If null, basic GetActorBounds fallback is used. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Bounds")
	TObjectPtr<UPCGExBoundsEvaluator> BoundsEvaluator;

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExActorCollectionEntry> Entries;

#if WITH_EDITOR
	// Editor Functions
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
#endif
};
