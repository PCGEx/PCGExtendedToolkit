// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"

#include "PCGExGenericCollectionEntry.generated.h"

/**
 * Generic collection entry: any asset the collection system has no entry type for (materials,
 * textures, particle systems, sounds...). Nothing spawns it; the asset travels as the staged path,
 * so every path-keyed consumer (Load Properties' AssetPath, Swap, Preload, cook) sees it. Bounds come
 * from the host's DefaultStagingBounds, tuned per entry by the staging bounds modifier.
 *
 * Entry-only type: no typed collection, registered by hand (see the .cpp), hosted by Omni collections.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Generic Collection Entry", meta=(ShortName="Generic"))
struct PCGEXCOLLECTIONS_API FPCGExGenericCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExGenericCollectionEntry() = default;

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Generic;
	}

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UObject> Asset;

	virtual bool Validate(const UPCGExAssetCollection* ParentCollection) override;
	virtual void UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive) override;
	virtual void SetAssetPath(const FSoftObjectPath& InPath) override;
};
