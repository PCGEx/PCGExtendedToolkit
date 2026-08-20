// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Sketch/PCGExClusterSketch.h"

#include "PCGExClusterSketchCollection.generated.h"

namespace PCGExSketch
{
	/** Collection type id. Owned by THIS module, never added to PCGExAssetCollection::TypeIds: this
	 *  module can be excluded from the build, and an excluded module must take its id with it. */
	inline const PCGExAssetCollection::FTypeId CollectionTypeId = FName(TEXT("ClusterSketch"));

	/**
	 * Registers the ClusterSketch type and its Omni drag-drop detector.
	 *
	 * MUST be called from StartupModule, never from a static initializer. PCGExCollections flushes the
	 * registry in ITS StartupModule, which runs before this module's static init -- so the queue is
	 * already closed and AddPendingRegistration would run the callback inline, at static-init time,
	 * where FTypeInfo's StaticClass()/StaticStruct() cannot be resolved. That is a crash, not a race.
	 */
	PCGEXELEMENTSCLUSTERSSKETCH_API void RegisterCollectionType();
}

/**
 * Cluster Sketch collection entry. References a UPCGExClusterSketch, or any collection through the
 * base SubCollection property. No MicroCache, no descriptors, no collection-level globals -- a sketch
 * carries its own snap provider and decorators, so nothing about it is host-configurable.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Cluster Sketch Collection Entry", meta=(ShortName="ClusterSketch"))
struct PCGEXELEMENTSCLUSTERSSKETCH_API FPCGExClusterSketchCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExClusterSketchCollectionEntry() = default;

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExSketch::CollectionTypeId;
	}

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UPCGExClusterSketch> Sketch = nullptr;

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;
};

/**
 * Weighted collection of Cluster Sketch assets. Staged picks resolve through the same type-blind
 * transport as every other collection, so a sketch entry travels the distribute/swap pipeline
 * unmodified and can be hosted in an Omni collection alongside meshes, actors and levels.
 */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Cluster Sketch", meta=(ToolTip = "A weighted collection of Cluster Sketch assets.", PCGExNodeLibraryDoc="staging/collections/cluster-sketch-collection"))
class PCGEXELEMENTSCLUSTERSSKETCH_API UPCGExClusterSketchCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExClusterSketchCollectionEntry)

public:
	friend struct FPCGExClusterSketchCollectionEntry;

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExSketch::CollectionTypeId;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExClusterSketchCollectionEntry> Entries;

#if WITH_EDITOR
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
#endif
};
