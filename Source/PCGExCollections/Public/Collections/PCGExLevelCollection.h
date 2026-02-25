// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExArrayHelpers.h"
#include "Helpers/PCGExActorContentFilter.h"
#include "Helpers/PCGExBoundsEvaluator.h"

#include "PCGExLevelCollection.generated.h"

class UPCGExLevelCollection;

/**
 * Level collection entry. References a UWorld level asset or
 * a UPCGExLevelCollection subcollection. UpdateStaging() loads the level
 * package in-editor to compute combined bounds from spatial actors.
 */
USTRUCT(BlueprintType, DisplayName="[PCGEx] Level Collection Entry")
struct PCGEXCOLLECTIONS_API FPCGExLevelCollectionEntry : public FPCGExAssetCollectionEntry
{
	GENERATED_BODY()

	FPCGExLevelCollectionEntry() = default;

	// Type System

	virtual PCGExAssetCollection::FTypeId GetTypeId() const override { return PCGExAssetCollection::TypeIds::Level; }

	// Level-Specific Properties

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TSoftObjectPtr<UWorld> Level = nullptr;

	UPROPERTY(EditAnywhere, Category = Settings, meta=(EditCondition="bIsSubCollection", EditConditionHides, DisplayAfter="bIsSubCollection"))
	TObjectPtr<UPCGExLevelCollection> SubCollection;

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

/** Concrete collection for level/world assets. */
UCLASS(BlueprintType, DisplayName="[PCGEx] Collection | Level")
class PCGEXCOLLECTIONS_API UPCGExLevelCollection : public UPCGExAssetCollection
{
	GENERATED_BODY()
	PCGEX_ASSET_COLLECTION_BODY(FPCGExLevelCollectionEntry)

public:
	friend struct FPCGExLevelCollectionEntry;

	// Type System
	virtual PCGExAssetCollection::FTypeId GetTypeId() const override
	{
		return PCGExAssetCollection::TypeIds::Level;
	}

	/** Actor content filter for bounds computation. If null, default infrastructure checks are used. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Bounds")
	TObjectPtr<UPCGExActorContentFilter> ContentFilter;

	/** Bounds evaluator for bounds computation. If null, bounds default to empty. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Bounds")
	TObjectPtr<UPCGExBoundsEvaluator> BoundsEvaluator;

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExLevelCollectionEntry> Entries;

#if WITH_EDITOR
	// Editor Functions
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
#endif
};
