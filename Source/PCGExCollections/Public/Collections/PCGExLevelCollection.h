// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"

#include "Core/PCGExAssetCollection.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Helpers/PCGExArrayHelpers.h"

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

	// ========== Bounds Filtering ==========

	/** If enabled, only actors with at least one of these tags contribute to bounds. Empty = all actors. */
	UPROPERTY(EditAnywhere, Category = "Settings|Bounds", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TArray<FName> BoundsIncludeTags;

	/** Actors with any of these tags are excluded from bounds computation. */
	UPROPERTY(EditAnywhere, Category = "Settings|Bounds", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TArray<FName> BoundsExcludeTags;

	/** If non-empty, only actors of these classes (or subclasses) contribute to bounds. */
	UPROPERTY(EditAnywhere, Category = "Settings|Bounds", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TArray<TSoftClassPtr<AActor>> BoundsIncludeClasses;

	/** If non-empty, actors of these classes (or subclasses) are excluded from bounds. */
	UPROPERTY(EditAnywhere, Category = "Settings|Bounds", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	TArray<TSoftClassPtr<AActor>> BoundsExcludeClasses;

	/** If enabled, only collidable primitive components contribute to bounds. */
	UPROPERTY(EditAnywhere, Category = "Settings|Bounds", meta=(EditCondition="!bIsSubCollection", EditConditionHides))
	bool bOnlyCollidingComponents = false;

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

	// Entries Array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	TArray<FPCGExLevelCollectionEntry> Entries;

#if WITH_EDITOR
	// Editor Functions
	virtual void EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData) override;
#endif
};
